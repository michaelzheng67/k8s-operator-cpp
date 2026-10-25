# Build Plan

A step-by-step path from an empty repo to the full operator described in [README.md](README.md). Each milestone produces something that runs and is tested before you move on.

**How to use this:** work top to bottom. Every milestone has a goal, tasks, and a **Done when** check. Don't start the next milestone until that check passes. Tick the boxes as you go.

---

## Dev setup (once)

```bash
# Toolchain
xcode-select --install
brew install cmake ninja llvm pkg-config docker kind kubectl minio/stable/mc

# Package manager for C++ deps
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=~/vcpkg   # add to your shell profile

# Local cluster
kind create cluster --name bt
kubectl cluster-info --context kind-bt
```

**Dev loop:** run the operator binary locally on your Mac. It reads `~/.kube/config` and talks to the kind cluster. Only package it into a container once it works (Milestone 9). That keeps the edit, build, run cycle down to seconds.

---

## Milestone 0: Project skeleton

**Goal:** an empty project that builds, tests, and lints in CI.

- [ ] `CMakeLists.txt` with targets `k8sop` (library), `operator` (executable), `tests`, `bench`
- [ ] `vcpkg.json` manifest: `boost-beast`, `boost-asio`, `openssl`, `nlohmann-json`, `spdlog`, `gtest`, `benchmark`, `yaml-cpp` (for kubeconfig)
- [ ] `CMakePresets.json`: `debug`, `release`, `asan` (ASan + UBSan), `tsan`
- [ ] `.clang-format`, `.clang-tidy`, `.gitignore`
- [ ] A trivial `main.cpp` and one passing GoogleTest
- [ ] GitHub Actions: build + test on the `debug`, `asan` and `tsan` presets, plus clang-tidy

**Done when:** `cmake --preset debug && cmake --build --preset debug && ctest --preset debug` passes locally and in CI.

---

## Milestone 1: API types + JSON mapping

**Goal:** C++ structs for every object we touch, with lossless JSON round-tripping.

- [ ] `api/meta.hpp`: `ObjectMeta` (name, namespace, uid, resourceVersion, labels, annotations, ownerReferences, finalizers, deletionTimestamp)
- [ ] `api/pod.hpp`: the subset of `Pod` we need (spec.containers with env, restartPolicy; status.phase, containerStatuses[].state.terminated.{exitCode, message})
- [ ] `api/backtest_job.hpp`: `BacktestJobSpec`, `BacktestJobStatus`
- [ ] `api/backtest_schedule.hpp`: `BacktestScheduleSpec`, `BacktestScheduleStatus`
- [ ] `to_json` / `from_json` for each, using nlohmann's ADL hooks
- [ ] Keep unknown fields when round-tripping objects we PATCH, so we never drop data we don't model

**Done when:** unit tests parse real JSON fixtures (captured with `kubectl get pod -o json`) and serialize them back without losing any field we model.

---

## Milestone 2: Synchronous API client

**Goal:** list and create Pods in the kind cluster from C++.

- [ ] `client/config`: parse kubeconfig (current context → server URL, CA data, client cert/key or token). Also support in-cluster config (`/var/run/secrets/kubernetes.io/serviceaccount/`).
- [ ] `client/http`: a blocking HTTPS client on Boost.Beast + OpenSSL: connect, TLS handshake, request, response, connection reuse
- [ ] `client/api_client`: typed calls returning `std::expected<T, ApiError>`
  - `get<T>(ns, name)`, `list<T>(ns, labelSelector)`, `create<T>(obj)`, `remove<T>(ns, name)`
  - `patch<T>(ns, name, body, PatchType::Merge|JSON|Apply)`, `patch_status<T>(...)`
- [ ] Map resource types to URL paths (core `/api/v1/...` vs. group `/apis/quant.example.com/v1alpha1/...`)
- [ ] `ApiError`: HTTP status, reason, message, and an `is_retryable()` check (429, 5xx, connection errors)
- [ ] A CLI subcommand for testing: `operator debug list-pods -n default`

**Done when:** `operator debug list-pods` prints the same Pods as `kubectl get pods`, and you can create and delete a Pod from C++.

---

## Milestone 3: Informers + cache

**Goal:** an in-memory mirror of cluster state that stays correct across disconnects.

- [ ] `client`: a streaming `watch<T>(ns, resourceVersion, labelSelector)` that reads the chunked response and yields one event per line (`ADDED`, `MODIFIED`, `DELETED`, `BOOKMARK`, `ERROR`)
- [ ] `informer/cache`: a thread-safe `Store<T>` keyed by `ns/name`; `get` returns `std::shared_ptr<const T>`
- [ ] `informer/index`: secondary indexes by owner UID (job → pods, schedule → jobs)
- [ ] `informer/informer<T>`: LIST, then fill the store, then WATCH from the list's `resourceVersion`
  - reconnect with exponential backoff and jitter
  - on `410 Gone`, relist and replace the store's contents
  - call registered `on_add` / `on_update` / `on_delete` handlers
  - `has_synced()` turns true after the first list
- [ ] Run each informer on its own `std::jthread` for now (async comes in Milestone 11)

**Done when:**
- An integration test against kind creates, updates and deletes Pods, and the store matches `kubectl` after each step.
- Restarting the API server (`docker restart bt-control-plane`) doesn't leave the store stale.
- TSan is clean.

---

## Milestone 4: Work queue + controller runtime

**Goal:** a generic "watch, then enqueue, then reconcile" engine that doesn't know about backtests yet.

- [ ] `queue/work_queue`: `add`, `get` (blocking), `done`, `forget`, `add_rate_limited`, `add_after`, `shutdown`
  - dedup: a key that is already queued isn't added twice
  - no concurrent processing: if a key is re-added while it's being processed, it waits until `done`
  - per-key exponential backoff (5 ms → 1000 s)
  - `add_after` backed by a min-heap of (time, key) and a timer thread
- [ ] `reconcile/reconciler.hpp`: `concept Reconciler = requires(R r, Key k) { { r.reconcile(k) } -> std::same_as<Result>; }`
- [ ] `runtime/controller<R>`: owns a queue and N worker threads; loop is `get`, `reconcile`, then `done` plus `forget`, requeue, or `add_after`
- [ ] `runtime/manager`: starts the informers, waits for `has_synced`, starts the controllers, and handles SIGINT/SIGTERM shutdown
- [ ] Wire up a toy reconciler that logs each Pod key it sees

**Done when:**
- Unit tests cover the dedup, no-concurrent-processing, backoff and delayed-add guarantees.
- The toy controller logs every Pod change in kind.
- Ctrl-C shuts everything down cleanly with no leaks under ASan.

---

## Milestone 5: CRDs + `JobReconciler` v1 (single trial, no merge)

**Goal:** `kubectl apply` a `BacktestJob` and get one Pod per shard, with retries and status.

- [ ] `deploy/crds/backtestjob.yaml`: OpenAPI schema, status subresource, printer columns (Phase, Trials, Best Sharpe, Age), and a CEL rule `self == oldSelf` on `spec`
- [ ] `JobReconciler` for a job **without** `parameters` or `merge`:
  - add a finalizer on first sight
  - split `universe.symbols` into `shardsPerTrial` shards (deterministic, order-preserving)
  - Pod naming: `<job>-t0-s<j>-a<attempt>`; labels `quant.example.com/{job,trial,shard,role,attempt}`; owner reference to the job
  - shard env vars per the worker contract; `restartPolicy: Never`
  - create missing shard Pods
  - when a Pod fails and `attempt < maxRetries`, create attempt + 1
  - compute `status.shards` counts and `phase`, then `patch_status`. On `409`, requeue.
  - on deletion, let owner references garbage-collect the Pods, then remove the finalizer
- [ ] Unit-test the reconciler against a fake cache and a fake client (record the intended creates and patches; no cluster needed)
- [ ] Test with a dummy worker image: `busybox` that sleeps and then exits 0, or exits 1 at random

**Done when:**
- Applying a 3-shard job creates 3 Pods.
- Killing one triggers a retry.
- `kubectl get backtestjobs` shows the phase going `Running` → `Succeeded`.
- Deleting the job removes everything.

---

## Milestone 6: Sample worker images + object storage

**Goal:** real backtest Pods that read market data and write outputs.

- [ ] Deploy MinIO in kind (`deploy/dev/minio.yaml`) and create a `backtests` bucket with `mc`
- [ ] A Secret with S3 credentials, injected into worker Pods by the operator (`envFrom`)
- [ ] `examples/backtester`: a small Python image (pandas + boto3)
  - loads daily bars for `BT_SYMBOLS` from a CSV fixture baked into the image (or from yfinance)
  - runs a simple momentum strategy parameterized by `BT_PARAMS`
  - writes a daily PnL series (Parquet or CSV) to `BT_OUTPUT_URI`
- [ ] `kind load docker-image` so the cluster can pull it locally

**Done when:** a single-trial job writes one PnL file per shard to MinIO.

---

## Milestone 7: Merge stage

**Goal:** each trial produces portfolio metrics that the operator can read.

- [ ] `examples/merger`: reads the shard PnL files from `BT_INPUT_URIS`, sums them into a portfolio series, and computes Sharpe, total PnL and max drawdown. Writes `results.json` to `BT_OUTPUT_URI` and a compact metrics JSON to `/dev/termination-log`.
- [ ] `JobReconciler`: trial states `Pending → RunningShards → Merging → Succeeded | Failed`
  - when every shard in a trial succeeds, create `<job>-t<i>-merge-a<n>`
  - when the merge Pod succeeds, parse `containerStatuses[0].state.terminated.message` into metrics
  - retry merge failures with the same `maxRetries` budget
- [ ] Store each trial's metrics on the job, either in `status` or in an annotation on the merge Pod, so a restarted operator can rebuild them from the cache

**Done when:** a single-trial job ends `Succeeded` with Sharpe, PnL and drawdown visible in `kubectl get backtestjob -o yaml`.

---

## Milestone 8: Parameter sweeps + ranking

**Goal:** the full `BacktestJob`.

- [ ] Grid expansion: sort parameter keys, then take the cartesian product in a fixed order, so trial `i` always means the same parameter combination
- [ ] Job-wide `maxConcurrentPods`: count running shard and merge Pods, and create new ones only while under budget. Prefer finishing started trials over starting new ones.
- [ ] Rank succeeded trials by `objective.metric` / `goal`, and write the top-K to `status.best`
- [ ] Have the operator write the full ranking to `resultsURI` in MinIO, **or** keep that out of scope so the operator stays storage-free and the merge Pods own all writes. Pick one and note it in the README.
- [ ] `status.trials` counts. Failed trials are listed and don't block the job.

**Done when:**
- A 3×2 grid over 6 symbols with `shardsPerTrial: 3` runs 18 shard Pods and 6 merge Pods without ever exceeding `maxConcurrentPods`.
- The job ends with a correct top-3 in `status.best`.

---

## Milestone 9: Run in-cluster

**Goal:** the operator runs as a Deployment, not on your laptop.

- [ ] `docker/Dockerfile`: multi-stage build (build on `debian:bookworm` with vcpkg, run on `gcr.io/distroless/cc`)
- [ ] `deploy/rbac.yaml`: ServiceAccount, ClusterRole (`backtestjobs`, `backtestjobs/status`, `backtestschedules`, `backtestschedules/status`, `pods`, `leases`, `events`), and ClusterRoleBinding
- [ ] `deploy/operator.yaml`: Deployment with resource limits; configured by env vars and flags
- [ ] `Makefile` or `justfile` targets: `build`, `test`, `image`, `kind-load`, `deploy`, `undeploy`, `e2e`

**Done when:** after `make deploy`, Milestone 8's sweep succeeds with the operator running inside kind.

---

## Milestone 10: `BacktestSchedule`

**Goal:** recurring backtests with history and alerting.

- [ ] `util/cron`: parse 5-field cron expressions and compute `next(after, tz)` and `prev(before, tz)`. Use `std::chrono::zoned_time` if your standard library ships the tz database; otherwise use Howard Hinnant's `date/tz.h` (vcpkg `date`). Apple Clang's libc++ may not have it yet.
- [ ] `deploy/crds/backtestschedule.yaml`
- [ ] `ScheduleReconciler`:
  - find the most recent missed tick since `lastScheduleTime`
  - when a tick is due, apply `concurrencyPolicy`, set `window = [tick - rollingWindow, tick]`, and create `<schedule>-<yyyymmddhhmm>` (a deterministic name, so a duplicate create returns 409 and is safe)
  - prune owned jobs beyond `historyLimit`, oldest first
  - copy finished jobs' `status.best[0]` into `history`, and set the `Degraded` condition from `alerts.minSharpe`
  - return `requeue_after(next_tick - now)`
- [ ] Unit-test the cron logic heavily: DST transitions, month ends, weekday-only expressions

**Done when:** a schedule set to `*/5 * * * *` creates a job every 5 minutes, keeps only `historyLimit` jobs, and flips `Degraded` when you set `minSharpe` above the observed Sharpe.

---

## Milestone 11: Async I/O

**Goal:** replace thread-per-watch and blocking requests with a single event loop.

- [ ] Port `client/http` to `boost::asio::ssl::stream` with C++20 coroutines (`co_await async_read`, `async_write`)
- [ ] Informers become coroutines on one `io_context`. Reconcile workers stay on a thread pool and call the client through `co_spawn` + `std::future` (or make the reconcilers coroutines too).
- [ ] Timeouts and cancellation via `asio::cancellation_signal` and `steady_timer`
- [ ] Graceful shutdown cancels all in-flight operations

**Done when:** all existing tests and the e2e sweep pass, the operator runs on 1 I/O thread + N workers, and TSan is clean.

---

## Milestone 12: Production features

- [ ] **Leader election:** acquire and renew a `coordination.k8s.io/v1` Lease (15 s lease, 10 s renew deadline, 2 s retry). Only the leader runs controllers. If renewal fails, stop the controllers and exit.
- [ ] **Metrics:** a minimal HTTP server on `:8080/metrics` in Prometheus text format: queue depth, reconcile count and errors, reconcile latency histogram, Pods by phase
- [ ] **Health:** `/healthz` (process up) and `/readyz` (caches synced and leader state known); wire them into Deployment probes
- [ ] **Events:** emit Kubernetes Events on the job (`ShardFailed`, `TrialSucceeded`, `JobSucceeded`)
- [ ] Run 2 replicas and verify that only one reconciles, and that failover works when you delete the leader Pod

**Done when:** you can kill the leader mid-sweep and the other replica takes over and finishes the job correctly.

---

## Milestone 13: Load testing + performance

**Goal:** make it fast, with numbers to show for it.

- [ ] Load generator: create 1,000 `BacktestJob`s with a no-op worker image (or simulate Pod events against a fake API server for pure operator throughput)
- [ ] Baseline metrics: event-to-reconcile latency (p50/p99/p99.9), reconciles per second, memory use, CPU per event
- [ ] Google Benchmark microbenchmarks for the JSON parse, the cache read/write, and the work-queue add/get paths
- [ ] Optimizations, each measured before and after:
  - simdjson on the watch path
  - a lock-free MPMC queue behind the `work_queue` interface
  - `std::pmr` arenas for per-event allocations
  - cache-line alignment on hot shared counters
  - an async, non-blocking logger
- [ ] Write the results to `docs/performance.md`

**Done when:** `docs/performance.md` has a table of baseline vs. optimized numbers for each change.

---

## Testing strategy (applies throughout)

| Layer | How |
|---|---|
| Pure logic (sharding, grid expansion, ranking, cron, state machines) | Unit tests, no I/O |
| Reconcilers | Unit tests with a fake cache and a fake client that records intended actions |
| Client / informers | Integration tests against kind |
| End-to-end | `make e2e`: deploy to a fresh kind cluster, apply example CRs, assert on final `status` |
| Concurrency | Every PR runs the test suite under TSan and ASan/UBSan |

## Conventions

- C++20, no raw `new`/`delete`, RAII for every resource.
- No exceptions cross module boundaries. Fallible APIs return `std::expected`.
- One reconciler call means one consistent read of the cache plus the minimum set of writes. Never sleep or block inside reconcile; return `requeue_after` instead.
- Every Pod the operator creates carries the `managed-by` label and an owner reference.
- Commit at the end of each milestone, tagged `m0`, `m1`, and so on, so you can always get back to a working state.
