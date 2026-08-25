// McBL# v2.0 — Math & Calculus Demo

inc(math_demo);

    // Basic math
    #pi  = math.PI
    #e   = math.E
    #sq  = math.sqrt(144)
    pr("sqrt(144) = " + sq)

    // Trig
    #sinval = math.sin(pi / 6)
    pr("sin(PI/6) = " + sinval)

    // Power + log
    #p = math.pow(2, 10)
    pr("2^10 = " + p)

    #ln2 = math.ln(math.E)
    pr("ln(e) = " + ln2)

    // Clamp + lerp
    #cl = math.clamp(150, 0, 100)
    pr("clamp(150, 0, 100) = " + cl)

    #lp = math.lerp(0, 100, 0.75)
    pr("lerp(0, 100, 0.75) = " + lp)

    // GCD + LCM
    #g = math.gcd(48, 18)
    pr("gcd(48, 18) = " + g)

    #lc = math.lcm(4, 6)
    pr("lcm(4, 6) = " + lc)

    // Factorial
    #f = math.fact(10)
    pr("10! = " + f)

    // Combinatorics
    #c = math.comb(10, 3)
    pr("C(10,3) = " + c)

    // Numerical derivative of x^2 at x=3 → should be ~6
    #dv = math.deriv((x) => x * x, 3.0, 0.001)
    pr("d/dx(x^2) at x=3 = " + dv)

    // Numerical integral of x^2 from 0 to 3 → should be 9
    #ig = math.integ((x) => x * x, 0.0, 3.0, 1000)
    pr("∫[0,3] x^2 dx = " + ig)

    // Summation Σ i from 1 to 100
    #sm = math.sum(i, 1, 100, i)
    pr("Σ i [1..100] = " + sm)

    // Matrix 2x2 identity
    #m = math.matrix(2, 2, {1,0,0,1})
    pr("identity matrix trace = " + math.trace(m))

    // Random
    math.seed(42)
    #r1 = math.rand()
    #r2 = math.rand()
    pr("rand1=" + r1 + " rand2=" + r2)

endinc;
