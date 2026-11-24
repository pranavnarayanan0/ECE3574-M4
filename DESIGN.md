# M5 Design — Thread-Safe AI Workspace

## 1. M4 fixes

I identified no separate M4 defect before starting M5. I used the existing student tests as my baseline.

## 2. Threads and ownership

`MainWindow` owns the live `Workspace`, active `ProcessingCore`, widgets, and `CorpusLifecycle` on the GUI thread. The CR-1 socket thread receives remote file paths and queues additions on the GUI thread. The first call to `build_corpus()` that starts a build creates one worker thread. Later builds reuse it.

The GUI and worker access `pending_build_` and `worker_stopping_` under `build_mutex_`. `worker_loop()` waits on `build_ready_`, takes the pending task, and releases the mutex before processing. Each task owns a workspace snapshot and builds a separate `ProcessingCore`. A queued Qt callback delivers the result to the GUI thread.

## 3. Corpus states

`CorpusLifecycle` delegates events to the current state object on the GUI thread.

| State | Meaning | Transitions |
|---|---|---|
| Empty | The workspace has no documents. | Add makes it ready to build. |
| Ready to build | Documents exist and need indexing. | Build enters Building. Further additions keep it ready to build. |
| Building | A build is outstanding. | Success enters Current. Failure or Add makes another build necessary. |
| Current | The index matches the workspace. | Add invalidates it. Rebuild enters Building. |
| Exiting | Shutdown has begun. | `stop_worker()` wakes and joins the worker. |

Reset returns any active state to Empty. Confirmed Exit enters Exiting. Search, Term Inspector, and Context require a current corpus.

## 4. Reset during a build

1. **GUI:** `reset_application()` calls `cancel_build()`, clears the pending task, increments `workspace_version_`, and resets the workspace and lifecycle.
2. **Worker:** `CancellableChunker::check_cancelled()` observes the old task’s atomic flag and stops processing. `worker_loop()` can then take a fresh request.
3. **GUI:** If completion was already queued, the callback checks the request’s cancellation handle. `finish_build()` also checks the workspace version before accepting a result.

## 5. Design decisions

- **Build during a build.** `build_corpus()` checks `lifecycle_.can_build()`. A repeated request leaves the running build in place.
- **Remote Add during a build.** `add_document_file()` cancels the build, advances the workspace version, and reports that the workspace changed. The user can rebuild with the added document.
- **Shared data.** The pending task conveys work, the stop flag ends the worker loop, and the per-build atomic flag cancels one request while keeping the worker available. `stop_worker()` notifies the condition variable before joining.

## 6. Fault detection

`AIWS_FAULT_NO_CANCEL` disables the check in `CancellableChunker::check_cancelled()`. `testResetReleasesWorkerForNextBuild()` caught the fresh build missing its two-second deadline.

`AIWS_FAULT_SILENT_SHUTDOWN` suppresses the notification in `stop_worker()`. `initTestCase()` detected that the child running `testExitWithIdleWorker()` was still blocked after eight seconds. The normal suite passed.

## 7. AI-use disclosure

I used ChatGPT to interpret requirements, generate and revise implementation and test code, troubleshoot build failures, and draft this document. I ran the normal and fault-enabled tests in the course Docker container.