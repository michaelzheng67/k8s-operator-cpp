# k8s-operator-cpp

A Kubernetes operator for running quant strategy backtests, written from scratch in modern C++ (C++20). It talks to the Kubernetes REST API directly and doesn't use a client library.

## What it does

You describe a backtest (or a whole parameter sweep, or a recurring one) as a Kubernetes resource. The operator turns it into a multi-stage pipeline of Pods and drives it to completion:

1. **Parameter sweep (fan-out):** it expands a grid of strategy parameters into individual *trials*.
2. **Sharding:** it splits each trial's symbol universe into *shards* and runs one worker Pod per shard, within a cluster-wide concurrency limit.
3. **Merging (fan-in):** when all of a trial's shards finish, it starts a merge Pod. The merge Pod combines the per-shard PnL series into portfolio metrics (Sharpe, PnL, max drawdown).
4. **Ranking:** it collects each trial's metrics, ranks the trials by an objective, and writes the top results into the resource's `status`.
5. **Scheduling:** optionally, it reruns the whole thing on a cron schedule over a rolling data window. It keeps a history of results and flags the strategy when performance degrades.

`kubectl get backtestjobs` then shows live progress and the best parameter set found so far.

### Why this needs an operator

Built-in resources like `Job` and `CronJob` can run N parallel Pods. They can't:

- run a **multi-stage DAG**, where shards feed a per-trial merge and the merges feed a ranking;
- make **decisions based on results**, such as reading each trial's metrics, ranking trials, or raising a `Degraded` condition when Sharpe drops below a threshold;
- **throttle across trials** (`maxConcurrentPods` for the whole sweep, not just per Job);
- keep a **domain-specific status**: trial and shard progress, the best parameters, and a historical performance trend.

## Custom resources

There are two CRDs, mirroring the `Job` / `CronJob` split:

| Kind | Purpose |
|---|---|
| `BacktestJob` | One run: a parameter sweep made of trials, each made of shards, ending in a ranked result |
| `BacktestSchedule` | Creates a `BacktestJob` on a cron schedule over a rolling window, keeps history, and alerts on degradation |

### `BacktestJob`

```yaml
apiVersion: quant.example.com/v1alpha1
kind: BacktestJob
metadata:
  name: momentum-sweep
spec:
  strategy: momentum
  image: ghcr.io/me/backtester:latest        # worker image, runs one shard
  universe:
    symbols: [AAPL, MSFT, NVDA, AMZN, GOOG, META]
    shardsPerTrial: 3                        # symbols are split across 3 pods per trial
  window:
    start: "2024-01-01"
    end: "2024-12-31"
  parameters:                                # grid: one trial per combination (3 x 2 = 6 trials)
    lookback: [20, 60, 120]
    threshold: [0.5, 1.0]
  merge:
    image: ghcr.io/me/merger:latest          # combines a trial's shard outputs into metrics
  objective:
    metric: sharpe
    goal: maximize
    topK: 3
  maxConcurrentPods: 10                      # across the whole sweep
  maxRetries: 2                              # per shard
  artifacts:
    uri: s3://backtests/momentum-sweep       # where shard/merge outputs are written

status:
  phase: Running                             # Pending | Running | Succeeded | Failed
  trials:   { total: 6,  succeeded: 3, running: 2, pending: 1, failed: 0 }
  shards:   { total: 18, succeeded: 12, running: 4, pending: 2, failed: 0 }
  best:
    - trial: 4
      parameters: { lookback: 60, threshold: 1.0 }
      metrics: { sharpe: 1.84, pnl: 0.231, maxDrawdown: -0.072 }
    - trial: 1
      parameters: { lookback: 20, threshold: 1.0 }
      metrics: { sharpe: 1.41, pnl: 0.187, maxDrawdown: -0.095 }
  resultsURI: s3://backtests/momentum-sweep/results.json
  conditions: [...]
```

The `spec` is immutable once created (enforced with a CEL validation rule, `self == oldSelf`). To change a run, create a new job.

**Hierarchy:** `BacktestJob` → trials (parameter combinations) → shards (symbol subsets) → Pods.

```
BacktestJob momentum-sweep
├── trial 0  {lookback: 20,  threshold: 0.5}
│   ├── shard 0  [AAPL, MSFT]  → pod
│   ├── shard 1  [NVDA, AMZN]  → pod
│   ├── shard 2  [GOOG, META]  → pod
│   └── merge                  → pod   (after all shards succeed)
├── trial 1  {lookback: 20,  threshold: 1.0}
│   └── ...
└── ... trial 5
                                → rank trials by objective, write top-K to status
```

**Trial lifecycle**

```
Pending ──▶ RunningShards ──▶ Merging ──▶ Succeeded
                  │               │
                  └───────┬───────┘
                          ▼
                       Failed      (a shard or the merge exhausted maxRetries)
```

The job is `Succeeded` when every trial has reached a terminal state and at least one trial succeeded. Results are ranked over the succeeded trials. Trials that fail are reported in `status` and don't block the rest of the sweep.

### `BacktestSchedule`

```yaml
apiVersion: quant.example.com/v1alpha1
kind: BacktestSchedule
metadata:
  name: momentum-daily
spec:
  schedule: "0 6 * * 1-5"                    # 06:00 on weekdays
  timeZone: America/New_York
  rollingWindow: 252d                        # each run backtests the trailing 252 days
  concurrencyPolicy: Forbid                  # Forbid | Replace | Allow
  historyLimit: 30                           # BacktestJobs to retain
  alerts:
    minSharpe: 1.0                           # raise Degraded if the best Sharpe falls below
  jobTemplate:
    spec:                                    # a BacktestJob spec without `window`
      strategy: momentum
      image: ghcr.io/me/backtester:latest
      # ...

status:
  lastScheduleTime: "2026-10-23T10:00:00Z"
  lastSuccessfulJob: momentum-daily-20261023
  history:
    - job: momentum-daily-20261023
      best: { parameters: { lookback: 60, threshold: 1.0 }, sharpe: 1.12 }
    - job: momentum-daily-20261022
      best: { parameters: { lookback: 60, threshold: 1.0 }, sharpe: 1.31 }
  conditions:
    - type: Degraded
      status: "False"
```

On each tick the schedule reconciler fills in `window` from `rollingWindow` and the run time, and creates a `BacktestJob` it owns. It enforces `concurrencyPolicy` and prunes old jobs past `historyLimit`. It also copies each finished job's best result into `history` and sets `Degraded` when the best Sharpe falls below `alerts.minSharpe`.

## Worker contract

The operator never runs backtest logic itself. Users supply two images that follow this contract.

**Shard worker** (`spec.image`) receives:

| Env var | Example |
|---|---|
| `BT_STRATEGY` | `momentum` |
| `BT_PARAMS` | `{"lookback":60,"threshold":1.0}` |
| `BT_SYMBOLS` | `AAPL,MSFT` |
| `BT_START` / `BT_END` | `2024-01-01` / `2024-12-31` |
| `BT_OUTPUT_URI` | `s3://backtests/momentum-sweep/trial-4/shard-0/` |

It writes its PnL series to `BT_OUTPUT_URI` and exits 0 on success.

**Merge worker** (`spec.merge.image`) receives `BT_INPUT_URIS`, a list of shard outputs, and `BT_OUTPUT_URI`. It computes portfolio metrics and does two things with them:

- writes the full results to `BT_OUTPUT_URI`;
- writes a small metrics JSON (`{"sharpe":1.84,"pnl":0.231,"maxDrawdown":-0.072}`) to `/dev/termination-log`.

The operator reads that termination message from the Pod's status to rank trials. So small metrics travel through the Kubernetes API, large artifacts go to object storage, and the operator never reads from the bucket.

## Architecture

```
            ┌────────────────────────────── Kubernetes API server ──────────────────────────────┐
            └──────▲──────────────────────────────┬─────────────────────────────────▲───────────┘
                   │ LIST / WATCH                 │ watch events                    │ CREATE / PATCH / DELETE
                   │                              ▼                                 │
┌──────────────────┴────────────────────────────────────────────────────────────────┴───────────┐
│ operator process                                                                              │
│                                                                                               │
│  ┌───────────────────┐  events  ┌─────────────────────┐  keys  ┌──────────────┐               │
│  │ Informers         │────────▶ │ Event handlers      │──────▶ │ Work queues  │               │
│  │  BacktestSchedule │          │  Pod → owning Job   │        │  (dedup,     │               │
│  │  BacktestJob      │          │  Job → owning Sched │        │   backoff,   │               │
│  │  Pod              │          └─────────────────────┘        │   delayed)   │               │
│  └────────┬──────────┘                                         └──────┬───────┘               │
│           │ writes                                                    │ pop                   │
│           ▼                                                           ▼                       │
│  ┌───────────────────┐       reads (desired + actual)        ┌─────────────────────┐          │
│  │ Local cache       │ ◀──────────────────────────────────── │ Reconcilers         │          │
│  │  ns/name → object │                                       │  ScheduleReconciler │──▶ API client
│  │  + owner indexes  │                                       │  JobReconciler      │          │
│  └───────────────────┘                                       └─────────────────────┘          │
│                                                                                               │
│  Leader election (Lease) · /metrics · /healthz · structured logging                           │
└───────────────────────────────────────────────────────────────────────────────────────────────┘
```

### Components

**API client** (`client/`)
- Loads credentials from kubeconfig, or from the in-cluster service-account token and CA cert.
- HTTPS over Boost.Beast + OpenSSL.
- Typed operations (`get`, `list`, `create`, `patch`, `remove`, `watch`) on resource structs that serialize to and from JSON.
- Returns errors as `std::expected<T, ApiError>`. It retries with backoff on transient errors and surfaces conflicts (`409`) to the caller.

**Informers and cache** (`informer/`)
- One informer each for `BacktestSchedule`, `BacktestJob`, and `Pod`. The Pod informer only sees Pods with the label `quant.example.com/managed-by=k8s-operator-cpp`.
- Starts with a `LIST`, then a `WATCH` from the returned `resourceVersion`. Watch results arrive as a chunked stream of newline-delimited JSON events.
- Reconnects with backoff. On `410 Gone` it does a full relist and resync.
- A thread-safe cache keyed by `namespace/name`, with secondary indexes by owner (job → pods, schedule → jobs). Readers get immutable snapshots (`std::shared_ptr<const T>`), so reads never block writes.

**Event handlers**
- Map every event to the key of the top-level object that needs reconciling. A Pod event enqueues its owning `BacktestJob`. A `BacktestJob` event enqueues the job itself and also its owning `BacktestSchedule`, if it has one.

**Work queues** (`queue/`)
- Same semantics as client-go's workqueue:
  - A key is never processed by two workers at once.
  - Duplicate adds while a key is queued collapse into one.
  - Failed keys are re-added with per-key exponential backoff.
- A **delayed add** (`add_after(key, duration)`), backed by a timer heap. The schedule reconciler uses it to wake up at the next cron tick.

**Reconcilers** (`reconcile/`)
- A `Reconciler` concept: `reconcile(const Key&) -> Result`, where `Result` is done, requeue, or requeue after a duration.
- Both reconcilers are level-based and idempotent. They recompute everything from the cache on each call, so a missed event or an operator restart is harmless.

`JobReconciler`, per call:
1. Load the job and its Pods from the owner index.
2. Expand `parameters` into a deterministic, ordered list of trials, and split `universe.symbols` into shards. The same spec always produces the same trial and shard IDs.
3. Rebuild each trial's state from its Pods, using labels `trial=<i>`, `shard=<j>`, `role=shard|merge`, and `attempt=<n>`.
4. Advance each trial's state machine:
   - Start missing shard Pods, subject to the job-wide `maxConcurrentPods` budget.
   - Replace failed shard Pods while `attempt < maxRetries`.
   - Start the merge Pod once every shard has succeeded.
   - Parse the merge Pod's termination message into trial metrics.
5. Rank the succeeded trials by `objective` and keep the top-K.
6. Patch `status` through the status subresource, using `resourceVersion` for optimistic concurrency. On a conflict, requeue.
7. Handle deletion through a finalizer. Pods are cleaned up via owner references and garbage collection.

`ScheduleReconciler`, per call:
1. Compute the most recent missed tick and the next tick from `schedule` and `timeZone`.
2. If a tick is due, apply `concurrencyPolicy` and create the `BacktestJob` for it. Job names are derived from the tick time, so a retried create is idempotent.
3. Prune jobs past `historyLimit`, update `history`, and evaluate `alerts` into the `Degraded` condition.
4. Return `requeue_after(next_tick - now)`.

**Runtime** (`runtime/`)
- Event loop on Boost.Asio. Watches and requests use C++20 coroutines.
- A fixed pool of reconcile workers per reconciler.
- Leader election through a `coordination.k8s.io/v1` `Lease`, so multiple replicas can run safely.
- A small HTTP server for `/metrics` (Prometheus: queue depth, reconcile latency, Pods by phase) and `/healthz`.
- Graceful shutdown on `SIGTERM`: stop the watches, drain the queues, release the lease.

### Concurrency model

- **I/O thread(s):** the Asio event loop drives all watch streams and API requests.
- **Worker threads:** N workers per reconciler pull from that reconciler's queue.
- **Shared state:** only the cache and the queues. The cache is single-writer per informer with snapshot reads. The queues are the only handoff point between informers and workers. Because the queue guarantees one worker per key, a job's reconcile never races with itself.

## Tech stack

| Concern | Choice |
|---|---|
| Language | C++20 |
| Build | CMake + Ninja, vcpkg |
| HTTP / TLS / async | Boost.Beast, Boost.Asio, OpenSSL |
| JSON | nlohmann/json (simdjson on the watch hot path) |
| Cron parsing | hand-rolled (5-field cron; time zones via `std::chrono` or `date/tz.h`) |
| Logging | spdlog |
| Tests | GoogleTest / GoogleMock, integration tests against `kind` |
| Benchmarks | Google Benchmark |
| Local object storage | MinIO (S3-compatible) for dev / tests |
| Tooling | clang-tidy, clang-format, ASan / UBSan / TSan |

## Layout

```
.
├── CMakeLists.txt
├── CMakePresets.json
├── include/k8sop/
│   ├── api/           # BacktestJob / BacktestSchedule / Pod types + JSON mapping
│   ├── client/        # HTTP + TLS, auth, typed API calls
│   ├── informer/      # list/watch, cache, owner indexes, event handlers
│   ├── queue/         # work queue, delayed queue
│   ├── reconcile/     # Reconciler concept, JobReconciler, ScheduleReconciler
│   ├── runtime/       # event loop, workers, leader election, metrics
│   └── util/          # logging, backoff, cron
├── src/
├── tests/
├── bench/
├── examples/
│   ├── backtester/    # sample shard worker image
│   └── merger/        # sample merge worker image
├── deploy/            # CRDs, RBAC, Deployment manifests
└── docker/
```

## Roadmap

See [PLAN.md](PLAN.md) for the detailed, milestone-by-milestone build plan.


- [ ] Project skeleton, CMake presets, CI with sanitizers
- [ ] Synchronous API client: auth, typed CRUD, JSON mapping
- [ ] Informers + cache with owner indexes, watch reconnect / relist
- [ ] Work queue (dedup, backoff, delayed adds) + reconcile workers
- [ ] `BacktestJob`, single trial: sharding, retries, status, finalizer
- [ ] Merge stage: merge Pod, termination-message metrics
- [ ] Parameter sweeps: trial expansion, `maxConcurrentPods`, ranking / top-K
- [ ] Sample worker + merger images, end-to-end test on `kind` + MinIO
- [ ] `BacktestSchedule`: cron, rolling window, concurrency policy, history, `Degraded`
- [ ] Async I/O with Asio + coroutines
- [ ] Leader election, metrics, health checks
- [ ] Load testing and performance work (lock-free queue, simdjson, allocators)
