# M4 Implementation Plan

## 1. Set Up the Project
-  Submit the completed `DESIGN_NOTE.md` to Gradescope.
-  Copy completed M3 source code into the M4 project.
-  Check that M4 starter headers match the existing implementation.
-  Build the project in Docker.
-  Run existing M3 tests.

## 2. Design the Background Building System
-  Identify which operations belong on the GUI thread.
-  Identify which operations belong on background threads.
-  Determine how to use C++ standard library threading.
-  Decide how the worker communicates with the GUI.
-  Decide how to prevent overlapping builds from causing problems.

## 3. Implement Corpus States
-  Define the possible corpus states.
-  Determine which events trigger state changes.
-  Track whether the Workspace changes during a build.
-  Decide when a completed build can be accepted.
-  Determine when a corpus must be invalidated.

## 4. Handle Dangerous Event Sequences
-  Handle adding documents while building.
-  Handle Reset during building.
-  Handle repeated Build Corpus requests.
-  Handle build failures.
-  Handle exiting while a build is running.
-  Ensure outdated results cannot be accepted.

## 5. Manage Corpus Ownership and Memory
-  Determine which component creates the corpus.
-  Decide who owns the corpus during building.
-  Determine how completed results transfer ownership.
-  Handle cleanup of outdated or failed results.
-  Ensure worker threads terminate safely.
-  Prevent memory leaks and dangling pointers.

## 6. Implement M4-Specific Features
-  Implement `add_document_file()`.
-  Implement `set_build_delay_ms()`.
-  Support `--build-delay-ms N`.
-  Ensure the delay affects the real build path without blocking the GUI.
-  Preserve existing M3 functionality and public APIs.

## 7. Test the Implementation
-  Test GUI responsiveness (R1).
-  Test corpus correctness (R2).
-  Test build failure handling (R3).
-  Test safe application exit and repeated builds (R4).
-  Implement `AIWS_FAULT_ACCEPT_STALE`.
-  Implement `AIWS_FAULT_LOSE_FAILURE`.
-  Write deterministic tests that detect both faults.
-  Run `student_tests` using CTest.

## 8. Implement Change Request CR-1
-  Read CR-1 when released November 1.
-  Identify the new requirements.
-  Determine which components need modification.
-  Implement the changes.
-  Update tests.
-  Document the changes in `DESIGN.md`.

## 9. Complete Documentation
-  Keep `DESIGN_NOTE.md` unchanged.
-  Document threading and ownership decisions in `DESIGN.md`.
-  Explain corpus validation.
-  Record changes from the original design.
-  Explain the impact of CR-1.
-  Identify fault-switch locations and the tests that detect them.
-  Include the required AI-use disclosure.
-  Generate `HISTORY.txt` using `git log --stat --date=iso`.

## 10. Final Submission
-  Build the project in the course Docker container.
-  Run all tests.
-  Verify that the GUI works correctly.
-  Verify the executable is named `ai_workspace_m4`.
-  Verify the CTest test is named `student_tests`.
-  Ensure no build directories are included in the submission.
-  Commit and push the completed project to GitHub.
-  Submit the complete M4 project through the required course submission process.

**Final deadline: Sunday, November 8, 2026, at 11:59 PM.**