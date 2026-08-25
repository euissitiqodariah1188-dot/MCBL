// McBL# v2.0 — Error Handling Demo

inc(error_demo);

    // try/catch baru untuk McBL#
    // Karena VM belum support try/catch native, kita demo dengan if/else
    #x = 10
    #y = 0

    if y == 0 do;
        pr("Error: Division by zero!")
    else;
        #z = x / y
        pr("Result = " + z)
    endinc;

    pr("Program continues after error check")

    // File error handling
    #data = readfile("nonexistent.txt")
    if data == "" do;
        pr("File not found - handled gracefully")
    endinc;

endinc;
