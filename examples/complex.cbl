inc(complex demo);
  CARGO_m(1048576)
  AUTOCLEAR_cargo

  dev factorial(n) {
    if n == 0 {
      return 1
    }
    return n * factorial(n - 1)
  }

  #result = factorial(10)
  pr(result)

  CLEAR_cargo
endinc;
