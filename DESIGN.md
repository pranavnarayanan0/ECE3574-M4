# M4 DESIGN.md

## 1. Threads and Ownership

MainWindow owns the Workspace, ProcessingCore, background build thread, and GUI components. The GUI thread handles user input, document addition, Reset, and displaying results.

When Build Corpus is requested, `build_corpus()` creates a copy of the Workspace. A `std::thread` uses this snapshot to build a separate ProcessingCore. The worker returns a BuildResult through a `std::future`.

A QTimer periodically calls `finish_build()` on the GUI thread to check whether the result is ready.

The worker does not modify the live Workspace, ProcessingCore, or GUI widgets. The Workspace snapshot and worker's ProcessingCore are private to the worker, while workspace version tracking and GUI state are accessed on the GUI thread. No mutable application data is intentionally shared between the GUI and build worker threads.

## 2. Corpus Validation

The application tracks corpus validity using `corpus_current_` and a `workspace_version_` counter.

Adding a document or resetting the application increments the version counter. Each background build records the version of the Workspace it started with.

When a build finishes, `finish_build()` compares the recorded version with the current version. If they differ, the result is rejected because it no longer represents the current Workspace.

Only valid completed builds replace the active ProcessingCore and enable corpus-dependent operations.

## 3. Changes Since Design Checkpoint

The implementation uses a Workspace snapshot and separate ProcessingCore to avoid concurrent access to the live backend.

A `std::future` transfers the build result to the GUI thread, and a QTimer checks for completion without blocking normal GUI interaction.

Repeated Build Corpus requests are ignored while a build is active.

## 4. CR-1 Impact

CR-1 adds a separate `aiws_send` command-line client and a POSIX Unix-domain socket server running on its own `std::thread`. The server receives document paths without blocking the GUI.

The communication protocol uses a four-byte unsigned big-endian path length followed by the UTF-8 path bytes. The response contains a one-byte status (0 for accepted, 1 for rejected), a four-byte unsigned big-endian message length, and an optional UTF-8 message.

This length-prefixed protocol supports spaces, Unicode, and newlines in filenames. The server handles partial reads and writes, validates file readability, and queues accepted requests to the GUI thread using `QMetaObject::invokeMethod()`.

The GUI processes remote additions through `add_document_file()`, which updates the Workspace and invalidates the existing corpus. During confirmed shutdown, the application waits for active builds to finish before stopping the socket server and removing the socket file.

## 5. Fault Detection

Two fault switches were implemented in `MainWindow::finish_build()`.

`AIWS_FAULT_ACCEPT_STALE` disables the Workspace version comparison, allowing an outdated corpus to become current. The stale-build rejection and Reset-during-build tests detect this defect.

`AIWS_FAULT_LOSE_FAILURE` prevents a failed build from being reported and leaves the application in its building state. The build-failure recovery test detects this defect.

Normal tests passed, while separate builds with each fault enabled failed in the expected tests.

## 6. AI-Use Disclosure

AI assistance was used to help understand M4 requirements, plan the background-thread architecture, develop C++ implementation changes, review code, and identify testing requirements. The implementation must be independently reviewed and verified.

---

## Pair Programming Log

**Partner:** Daniel Robles-Hinckley

**Project:** M4 CR-1 — Remote Add

### Session 1 — My M4 Code

**Date:** 11/8/2026
**Start:** 8:00 PM  
**End:** 9:00 PM  
**Code worked on:** My M4 project

| Time | Driver | Navigator | Work Completed |
|---|---|---|---|
| 8:00–8:15 PM | Partner | Me | Set up the Unix-domain socket connection and discussed how the application would receive file paths. |
| 8:15–8:30 PM | Me | Partner | Developed the message structure and handled requests received across multiple reads. |
| 8:30–8:45 PM | Partner | Me | Integrated incoming document requests with the existing Add Document functionality. |
| 8:45–9:00 PM | Me | Partner | Checked error handling and tested the behavior with readable and unreadable files. |

During this session, we concentrated on implementing remote document addition while keeping the GUI responsive. As the navigator, I suggested making sure remote document additions properly invalidated the existing corpus so outdated results could not be used. My partner contributed by recommending additional tests for messages arriving in separate pieces rather than all at once.

### Session 2 — Partner's M4 Code

**Date:** 11/6/2026
**Start:** 8:00 PM  
**End:** 9:00 PM  
**Code worked on:** My partner's M4 project

| Time | Driver | Navigator | Work Completed |
|---|---|---|---|
| 8:00–8:15 PM | Me | Partner | Examined the socket server implementation and its communication with the main application. |
| 8:15–8:30 PM | Partner | Me | Developed the `aiws_send` utility and checked its return codes for successful and unsuccessful requests. |
| 8:30–8:45 PM | Me | Partner | Tested document additions during background corpus builds and examined how stale results were handled. |
| 8:45-9:00 PM | Partner | Me | Checked socket removal during application shutdown and tried additional document path cases. |

For the second session, we focused on reviewing and improving my partner's CR-1 implementation. While navigating, I recommended verifying that the socket file was properly removed after the application exited, preventing issues when restarting the program. My partner suggested testing remote document additions during an ongoing corpus build to ensure the application would reject outdated results.

### Overall Reflection

Alternating between driving and navigating every 15 minutes gave us both opportunities to implement code and evaluate each other's decisions. Explaining our changes while driving was particularly useful because it allowed the navigator to identify potential issues and suggest improvements immediately. Pair programming also helped us understand how the socket communication, background processing, and GUI functionality needed to work together.
