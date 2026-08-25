inc(McBL# IDE);
  CARGO_m(2097152)
  AUTOCLEAR_cargo

  dev fibonacci(n) {
    if n <= 1 {
      return n
    }
    return fibonacci(n - 1) + fibonacci(n - 2)
  }

  dev isprime(n) {
    if n <= 1 {
      return 0
    }
    #i = 2
    while i < n {
      if n % i == 0 {
        return 0
      }
      #i = i + 1
    }
    return 1
  }

  dev sumrange(start, end) {
    #total = 0
    for i range(start, end) {
      #total = total + i
    }
    return total
  }

  pr("=== McBL# IDE Demo ===")
  pr("")
  pr("Fibonacci sequence:")
  for i range(0, 10) {
    #fib = fibonacci(i)
    pr(fib)
  }

  pr("")
  pr("Prime check (1-20):")
  for i range(1, 20) {
    if isprime(i) {
      #label = i + " is prime"
      pr(label)
    }
  }

  pr("")
  pr("Sum 1 to 100:")
  #s = sumrange(1, 100)
  pr(s)

  pr("")
  pr("OR-gate demo:")
  #file1 = 1
  #file2 = 1
  @ready = 100
  pr(ready)

  pr("")
  pr("CPU registers:")
  using cpu
  MOV EAX, 42
  MOV EBX, 10
  #eax_val = EAX
  #ebx_val = EBX
  pr(eax_val)
  pr(ebx_val)

  pr("")
  pr("String operations:")
  #greeting = "Hello"
  #name = "McBL#"
  #full = greeting + " " + name
  pr(full)

  pr("")
  pr("Arithmetic:")
  #a = 15
  #b = 4
  pr(a + b)
  pr(a - b)
  pr(a * b)
  pr(a / b)
  pr(a % b)

  pr("")
  pr("Comparisons:")
  pr(a > b)
  pr(a < b)
  pr(a == 15)
  pr(a != b)

  pr("")
  pr("Loop with break:")
  #count = 0
  loop {
    #count = count + 1
    if count >= 5 {
      break
    }
  }
  pr(count)

  pr("")
  pr("=== IDE Demo Complete ===")
  CLEAR_cargo
endinc;
