# McBL# — v2.0 UPGRADED

**High-performance systems language** — cocok untuk bikin:
- Compiler & bahasa pemrograman
- Game engine
- Operating System
- AI / Neural networks
- Project kompleks apapun

---

## Build

```bash
# Normal build
make

# Build super cepat (native optimization)
make FAST=1

# Build debug + memory check
make DEBUG=1
```

---

## Jalanin

```bash
./MSDK run     file.cbl          # Jalanin via MDK VM
./MSDK mvm     file.cbl          # Jalanin via MVM 4-core (lebih cepet)
./MSDK compile file.cbl          # Compile ke native binary
./MSDK compile file.cbl -o out   # Compile ke output spesifik
./MSDK check   file.cbl          # Type-check saja
./MSDK bench   file.cbl          # Run + tampilkan benchmark
./MSDK memcheck file.cbl         # Run + cek memory leak
./MSDK lex     file.cbl          # Dump token stream
./MSDK ast     file.cbl          # Dump AST
./MSDK bc      file.cbl          # Dump bytecode
./MSDK version                   # Info versi
```

---

## Syntax McBL# v2.0

### Program dasar
```mcbl
inc(namaku);
    #nama = "Gerrard"
    pr("Hello " + nama)
endinc;
```

### OOP
```mcbl
class Animal {
    pub #name = ""
    pub dev speak() {
        pr(name + " makes a sound")
    }
}

class Dog extends Animal {
    override pub dev speak() {
        pr(name + " says: Woof!")
    }
}

#d = new Dog()
d.name = "Rex"
d.speak()
```

### classInt (syntax asli)
```mcbl
inc(contoh);
    classInt(varClass);
        #hello = "world"
    classInt(printClass);
        pr(hello)
    printClass()
endinc;
```

### Tipe data statis
```mcbl
int  #umur   = 25
float #berat  = 70.5
str  #nama   = "Gerrard"
bool #aktif  = true
auto #nilai  = 100       // type inference
```

### Array
```mcbl
$array murid = {dafa, davin, gerrard}

murid.push("zaki")
pr(murid.len)          // 4
pr(murid[0])           // dafa
pr(murid[1:3])         // slice
murid.pop()
```

### Map
```mcbl
map<str, int> nilai = {}
nilai["dafa"]    = 90
nilai["gerrard"] = 95
pr(nilai["gerrard"])
pr(nilai.has("dafa"))
$array keys = nilai.keys()
```

### Pointer (>>...<<)
```mcbl
>>pointer_ku =
    #x = 10
    #y = 20
    pr(x + y)
<<
pr(pointer_ku)
```

### func
```mcbl
func(tambah);
    #a = inputxt("a: ")
    #b = inputxt("b: ")
    pr(a + b)
```

### Error handling
```mcbl
try {
    throw "error!"
} catch(err) {
    pr("Caught: " + err)
} finally {
    pr("Done")
}
```

### Math (keyword kalkulus)
```mcbl
pr(math.sqrt(16))                          // 4
pr(math.pow(2, 10))                        // 1024
pr(math.sin(math.PI / 2))                 // 1.0
pr(math.deriv((x) => x*x, 3.0, 0.001))   // ~6.0
pr(math.integ((x) => x*x, 0.0, 3.0, 1000)) // ~9.0
pr(math.sum(i, 1, 100, i))               // 5050
pr(math.matrix(2, 2, {1,0,0,1}))
```

### String
```mcbl
pr(str.len("hello"))
pr(str.upper("hello"))
pr(str.find("hello world", "world", 0))
pr(str.format("Halo {} umur {}", "Dafa", 20))
pr(str.regex("abc123", "[0-9]+"))
$array parts = str.split("a,b,c", ",")
pr(str.join("-", parts))
```

### File
```mcbl
file.write("data.txt", "isi file")
#isi = readfile("data.txt")
pr(isi)
pr(file.exists("data.txt"))
$array lines = file.lines("data.txt")
```

### Network
```mcbl
#resp = net.get("https://api.example.com")
pr(resp.body)
```

### System
```mcbl
pr(sys.time())
pr(sys.env("PATH"))
#output = sys.exec("ls -la")
pr(output)
```

### Async/Await
```mcbl
async dev fetchData(url) {
    return net.get(url)
}
#task   = async fetchData("https://api.example.com")
#result = await task
pr(result)
```

### MVM (4-core parallel)
```mcbl
mvm_spawn(4) {
    #result = math.sqrt(huge_number)
    mvm_pipe(output_chan, result)
}
mvm_sync()
```

### MBLL — Low Level Bridge
```mcbl
// Pake file C
externFile(mycode.c)

// Pake library .cll
include<mylib.cll>

// Pinjam RAM
WriteTRam(1000)   // 1 GB
// ... kode kompleks ...
cleanTram()
```

### Compile C → .cll
```bash
MSDK compile/myfile.c -CLL mylib
# Hasilnya: mylib.cll
```

### Debug
```mcbl
debug.assert(x > 0, "x harus positif")
debug.log("INFO", "value = " + x)
debug.trace("checkpoint", __file__, __line__)

// Benchmark
debug.bench {
    // kode yang mau di-benchmark
}
```

---

## Arsitektur v2.0

```
McBL# Source (.cbl)
     │
     ▼
  Lexer (lexer.c)
  250+ token types
     │
     ▼
  Parser (parser.c)
  Full grammar
     │
     ▼
  AST (ast.h)
  180+ node types
     │
     ├──► Bytecode Gen → MDK VM → Output
     │       (bcgen)      (mdk.c)
     │
     ├──► C Transpiler → GCC → Native x86_64
     │       (cgen)
     │
     └──► MVM Direct → 4-core native
             (mvm.c) 40%ASM 50%C 10%C++
```

### Subsystem v2.0
| Subsystem | File | Tugas |
|-----------|------|-------|
| MVM | mvm.c | McBL Virtual Machine, 4-core, inline x86 ASM, JIT |
| MBLL | mbll.c | Bridge ke C/C++, .cll libraries, TRam, externFile |
| OOP | oop.c | Class, inheritance, vtable, GC |
| Math | mcbl_math.c | Kalkulus, vektor, matriks, statistik |
| String | mcbl_str.c | String manipulation + regex |
| Sys | mcbl_sys.c | File, network, system, debug, profiler |

---

## File Extensions
| Extension | Keterangan |
|-----------|-----------|
| `.cbl` | McBL# source file |
| `.modcbl` | McBL# module/library |
| `.cxcbl` | Hybrid McBL#+C |
| `.cll` | McBL# compiled library (seperti .dll) |

---

## Performance Tips
1. Pakai `make FAST=1` untuk build dengan `-O3 -march=native`
2. Pakai `./MSDK mvm file.cbl` untuk multi-core execution
3. Pakai `WriteTRam(n)` untuk project kompleks yang butuh banyak memory
4. Pakai `mvm_opt` untuk enable optimizer pass tambahan
5. Tipe statis (`int #x = 5`) lebih cepat dari auto-infer
6. Pakai `$array` untuk data yang diakses berulang

