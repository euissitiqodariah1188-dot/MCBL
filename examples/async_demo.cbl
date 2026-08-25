// McBL# v2.0 — Async / Thread Demo
// (async/await dieksekusi via thread keyword di VM)

inc(async_demo);

    // Thread untuk concurrent execution
    thread {
        pr("Worker thread 1 running")
        #result1 = 100 * 100
        pr("result1 = " + result1)
    }

    thread {
        pr("Worker thread 2 running")
        #result2 = 200 * 200
        pr("result2 = " + result2)
    }

    // MVM multi-core dispatch
    pr("Main thread continues")

    // Channel-style passing via variable
    #shared = 0

    pr("Async demo done")

endinc;
