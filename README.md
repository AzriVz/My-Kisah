# Decompiler

Decompiler adalah aplikasi desktop C++20 berbasis Qt 6 untuk menganalisis executable ELF64 x86-64. Aplikasi menampilkan metadata binary, Symbol Tree, assembly beserta alur branch, Call Graph, dan pseudocode C++-like.

Pseudocode yang dihasilkan merupakan rekonstruksi semantik dari instruksi binary. Output tidak diklaim sebagai source code asli karena nama variabel, tipe, dan struktur source umumnya sudah hilang setelah proses kompilasi.

## Daftar isi

- [Fitur](#fitur)
- [Dependency](#dependency)
- [Build dan run](#build-dan-run)
- [Arsitektur repository](#arsitektur-repository)
- [Seluruh proses dekompilasi](#seluruh-proses-dekompilasi)
- [Strategi implementasi](#strategi-implementasi)
- [Generated test case dan hasil pengujian](#generated-test-case-dan-hasil-pengujian)
- [Implementasi fitur bonus](#implementasi-fitur-bonus)
- [Known limitations](#known-limitations)

## Fitur

Fitur wajib yang tersedia:

- validasi ELF64, little-endian, x86-64, executable/PIE, section, program header, dan symbol table;
- function discovery dari symbol, entry point, target direct call, dan heuristik stripped binary;
- disassembly dengan address, opcode bytes, mnemonic, operand terstruktur, serta klasifikasi control-flow;
- basic block, Control Flow Graph, predecessor/successor, reachability, DFS, reverse postorder, dominator, back edge, dan loop header;
- analisis System V AMD64 ABI, alias register, parameter register/stack, local stack, return value, dan call-clobbered register;
- Intermediate Representation dan data-flow untuk assignment, load/store, arithmetic, bitwise, comparison, call, jump, conditional select, dan return;
- pseudocode untuk arithmetic, direct call, `if`, `if/else`, loop sederhana atau fallback `goto`, string read-only, dan penanda instruksi unsupported;
- GUI tiga kolom: Symbol Tree di kiri, Assembly/Call Graph di tengah, dan Pseudocode di kanan;
- quick search berdasarkan nama/address, navigasi call/branch, pencarian pseudocode, serta history Back/Forward;
- analisis di worker thread, cache hasil per fungsi, dan pembersihan state ketika membuka binary berikutnya.

Fitur bonus yang tersedia:

- Call Graph interaktif dengan zoom, pan, fit, minimap, component list, dan navigasi node;
- dukungan eksperimental PIE dan binary hasil optimasi `-O2`/`-O3`;
- binary patching ke file baru dengan re-analysis dan refresh Call Graph otomatis.

## Dependency

Kebutuhan utama:

- Linux x86-64;
- GCC atau Clang dengan dukungan C++20;
- CMake 3.20 atau lebih baru;
- Qt 6 Widgets, minimum 6.2;
- Capstone Engine;
- `g++` untuk menghasilkan binary test.

Contoh instalasi pada Ubuntu/Debian:

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev libcapstone-dev
```

Qt digunakan untuk GUI dan Capstone digunakan sebagai instruction decoder. Model fungsi, basic block, CFG, ABI analysis, IR, data-flow, control recovery, pseudocode, navigasi, dan graph layout tetap diimplementasikan oleh proyek ini. Aplikasi tidak membungkus Ghidra, IDA, RetDec, angr decompiler, atau decompiler lengkap lainnya.

## Build dan run

### Build manual

Dari root repository:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Executable akan tersedia di:

```text
build/decompiler
```

### Build menggunakan script

```bash
./scripts/build.sh
```

Untuk memakai direktori build yang berbeda:

```bash
./scripts/build.sh build-release
```

Jumlah parallel job dapat diatur melalui `BUILD_JOBS`:

```bash
BUILD_JOBS=8 ./scripts/build.sh
```

### Menjalankan aplikasi

Membuka aplikasi tanpa binary awal:

```bash
./build/decompiler
```

Binary juga dapat diberikan melalui command line:

```bash
./build/decompiler ./build/test-binaries/arithmetic
```

Di dalam GUI, gunakan **File > Open Binary**, shortcut `Ctrl+O`, atau tombol Open Binary pada toolbar. Binary utama yang diharapkan dibuat dengan konfigurasi:

```bash
g++ program.cpp \
    -O1 \
    -fno-inline \
    -fno-omit-frame-pointer \
    -no-pie \
    -o program
```

Input yang bukan ELF64 little-endian x86-64, file kosong/rusak, shared library biasa, atau binary tanpa executable `.text` akan ditolak dengan pesan kesalahan.

### Build dan test sekaligus

```bash
./scripts/run_tests.sh
```

Untuk pengujian GUI pada mesin tanpa display, CTest sudah mengatur `QT_QPA_PLATFORM=offscreen` pada test yang membutuhkannya.

## Arsitektur repository

Project dibagi menjadi dua library agar engine analisis tidak bergantung pada GUI:

```text
decompiler_core
├── ELF loader dan metadata
├── function discovery dan disassembler
├── basic block dan CFG
├── ABI, IR, dan data-flow
├── pseudocode generator
├── Call Graph model
└── binary patcher

decompiler_gui
├── MainWindow dan navigation history
├── Symbol Tree
├── Assembly Graph Table
├── Pseudocode View
└── Call Graph View, layout, sidebar, dan minimap
```

`AnalysisSession` menjadi pemilik state analisis. Widget hanya membaca hasil session dan tidak menyimpan model analisisnya sendiri. Cache di-key menggunakan alamat awal fungsi dan berisi instruction list, CFG, hasil ABI, IR, data-flow, serta pseudocode.

Alur besarnya adalah:

```text
ELF file
  -> validation dan metadata
  -> section/symbol reader
  -> function discovery
  -> disassembly
  -> basic block dan CFG
  -> ABI analysis dan IR lifting
  -> data-flow dan expression recovery
  -> control-structure recovery
  -> pseudocode dan Call Graph
  -> GUI presentation dan navigation
```

## Seluruh proses dekompilasi

### 1. Membuka file dan menjalankan analysis worker

`MainWindow` membersihkan view, history, dan metadata binary sebelumnya, lalu menampilkan progress indicator. `AnalysisSession::analyze` dijalankan melalui worker `std::async`.

Worker tidak mengakses widget. GUI thread tetap menangani paint, timer, progress, dan window-system event. Input sementara ditahan agar pengguna tidak memulai load kedua secara re-entrant ketika session pertama belum selesai. Setelah worker selesai, session baru dipasang dan seluruh update widget dilakukan dari GUI thread.

### 2. Validasi dan parsing ELF

`ElfLoader` membaca file sebagai byte buffer dan melakukan validasi sebelum memakai offset dari file:

1. path harus menunjuk regular file yang dapat dibaca;
2. file tidak boleh kosong atau lebih besar daripada ukuran yang aman untuk ditampung;
3. magic harus `0x7F 'E' 'L' 'F'`;
4. class harus ELF64;
5. endianness harus little-endian;
6. machine harus `EM_X86_64`;
7. type harus executable atau PIE, bukan shared library biasa;
8. program-header, section-header, dan symbol-table range harus berada di dalam file;
9. `.text` harus tersedia, non-empty, dan executable.

Loader kemudian membaca program header, section header, `.symtab`, `.dynsym`, string table, entry point, `.text`, serta section read-only seperti `.rodata`. Loader juga menyediakan mapping virtual-address ke file-offset dan sebaliknya.

Semua range diperiksa menggunakan pola `offset <= size` dan `length <= size - offset`. Cara ini menghindari overflow yang dapat terjadi pada pemeriksaan `offset + length <= size`.

### 3. Function discovery

Candidate fungsi dikumpulkan dari beberapa sumber:

- symbol bertipe `STT_FUNC` pada `.symtab` atau `.dynsym`;
- ELF entry point;
- target direct `call` yang berada di `.text`;
- marker `endbr`, frame prologue, atau instruction ber-alignment setelah terminator/padding pada binary stripped.

Jika beberapa sumber menunjuk alamat yang sama, sumber dengan informasi terbaik diprioritaskan. Nama symbol dipakai jika ada; candidate tanpa nama menjadi `sub_<alamat-hex>`. Nama duplikat diberi suffix alamat agar selalu unik.

Symbol size digunakan selama tidak melewati executable range atau alamat candidate berikutnya. Bila size tidak tersedia, alamat fungsi berikutnya menjadi batas sementara.

### 4. Disassembly

Setiap range fungsi diberikan kepada Capstone dalam x86-64 detail mode. Hasil Capstone disalin ke model `Instruction` internal yang menyimpan:

- address;
- raw opcode bytes;
- mnemonic dan operand text;
- operand register/immediate/memory terstruktur;
- register yang dibaca dan ditulis;
- jenis normal, call, return, conditional jump, unconditional jump, atau indirect jump;
- direct branch/call target jika tersedia.

Capstone hanya dipakai sebagai decoder. Seluruh analisis setelah decoding menggunakan model milik proyek. Byte yang tidak dapat didekode menjadi instruction `Invalid` sehingga analisis dapat memberi fallback tanpa crash.

### 5. Basic block dan Control Flow Graph

`BasicBlockBuilder` menentukan leader dari:

- instruction pertama fungsi;
- target direct branch;
- instruction setelah conditional/unconditional branch;
- instruction setelah return atau indirect jump untuk direpresentasikan sebagai kemungkinan unreachable block.

CFG membuat edge dengan aturan:

- conditional jump: target dan fall-through;
- unconditional jump: target saja;
- return: tanpa successor;
- normal/call: fall-through;
- indirect jump yang tidak diketahui: unresolved successor.

Setelah edge terbentuk, engine menghitung predecessor, successor, entry/exit, unreachable block, DFS, reverse postorder, dominator, back edge, dan loop header. Traversal menggunakan container iteratif agar recursive call atau cyclic CFG tidak menyebabkan infinite recursion pada analyzer.

### 6. Analisis System V AMD64 ABI

`AbiAnalyzer` menormalisasi alias register, misalnya `eax` ke keluarga `rax` dan `r9d` ke `r9`, sambil mempertahankan bit width dan aturan zero-extension.

Parameter integer/pointer diperkirakan dari register yang dibaca sebelum ditulis dengan urutan:

```text
RDI, RSI, RDX, RCX, R8, R9
```

Stack argument dimulai dari `[rbp+16]`. Offset negatif berbasis `rbp` menjadi local variable seperti `local_4` atau `local_8`. Write ke keluarga `rax` yang mencapai `ret` digunakan untuk memperkirakan return value. Daftar call-clobbered register dipakai saat menganalisis efek function call.

### 7. Lifting ke Intermediate Representation

Assembly tidak langsung diubah menjadi string C++. Setiap instruction terlebih dahulu diangkat ke IR yang memiliki operation seperti:

```text
Assign, Load, Store, Add, Subtract, Multiply, Divide,
BitAnd, BitOr, BitXor, Shift, Negate, Compare,
ConditionalSelect, Cast, Call, Return, Jump, Unknown
```

IR memisahkan operation dari syntax x86 dan menghubungkan value dengan register, immediate, parameter, stack variable, memory, atau function target. Instruksi yang belum didukung menjadi `Unknown` dengan address dan mnemonic asal.

Untuk alamat RIP-relative yang menunjuk ASCII null-terminated pada section read-only, session mengubah address tersebut menjadi escaped string literal, misalnya `"hello"`.

### 8. Data-flow dan expression recovery

`DataFlowAnalyzer` melacak expression register dan stack secara lokal. Contoh:

```asm
mov eax, edi
add eax, esi
ret
```

dipulihkan menjadi:

```cpp
return (arg0 + arg1);
```

Pada CFG satu block, expression dapat dipropagasikan langsung. Pada CFG dengan beberapa block, register dimaterialisasi sebagai `result` atau `tempN`. Hal ini mencegah satu nilai dari suatu branch digunakan secara keliru pada branch lainnya.

State comparison dari `cmp`, `test`, atau operation yang relevan dipasangkan dengan conditional jump, `setcc`, atau `cmovcc`. Sebelum function call, argument diambil dari state register parameter; setelah call, register yang termasuk call-clobbered di-invalidasi.

### 9. Control-structure recovery

Untuk graph tanpa back edge, generator mencari join terdekat dari dua successor conditional block. Pola tersebut digunakan untuk menghasilkan `if` atau `if/else`.

GCC `-O1` dapat mengubah source `if` menjadi branchless code seperti `setg` atau `cmovs`. Karena tidak ada branch CFG pada pola tersebut, IR `ConditionalSelect` merepresentasikan dua nilai alternatif, lalu generator mengubahnya kembali menjadi conditional assignment berbentuk `if/else`.

Back edge digunakan sebagai indikasi loop. Loop sederhana dihasilkan sebagai `while` atau `do-while`. Jika struktur graph belum dapat dipulihkan dengan aman, output memakai label dan `goto`. Pendekatan ini menjaga output tetap jujur daripada membuat struktur yang terlihat rapi tetapi semantiknya salah.

### 10. Pseudocode generation

`PseudocodeGenerator` menghasilkan:

- disclaimer bahwa output bukan source original;
- comment alamat fungsi;
- perkiraan return type;
- nama fungsi yang sudah disanitasi;
- parameter dan local variable fallback;
- assignment, arithmetic, call, return, dan struktur kontrol;
- label/`goto` untuk fallback;
- comment address-aware untuk instruction unsupported.

Contoh:

```cpp
// Reconstructed pseudocode; it may not match the original source.
// Address: 0x401106
int add(int arg0, int arg1) {
    return (arg0 + arg1);
}
```

### 11. Call Graph dan cache akhir

Setelah instruction seluruh fungsi tersedia, `CallGraph` mengumpulkan direct call menjadi edge caller-to-callee. Target yang tidak termasuk fungsi internal ditampilkan sebagai external node. Relasi disimpan sebagai pasangan unik sehingga pemanggilan berulang tidak menghasilkan edge duplikat.

Instruction, CFG, ABI, IR, data-flow, pseudocode, dan Call Graph disimpan di `AnalysisSession`. Klik fungsi, search, atau navigation hanya membaca cache; full analysis tidak dijalankan ulang setiap kali selection berubah.

### 12. Presentasi dan navigasi GUI

Workspace menggunakan splitter tiga kolom:

- kiri: Symbol Tree berisi Imports, Exports, Functions, Labels, Data, Sections, Classes, dan Namespaces;
- tengah: Assembly dan Call Graph;
- kanan: Pseudocode dan text search.

Assembly bersifat read-only dan memiliki kolom address, bytes, mnemonic, dan operand. Scrollbar horizontal native, dengan perilaku per-pixel yang sama seperti Call Graph, muncul di bawah listing ketika kolom assembly lebih lebar dari panel. Double-click direct call membuka callee, sedangkan double-click branch memindahkan selection ke target address. Pseudocode memiliki font monospace, line number, syntax highlighting, copy/search, dan aktivasi function call. Semua jalur navigasi berbagi history Back/Forward.

## Strategi implementasi

Bagian ini menjelaskan alasan di balik approach utama yang digunakan.

### Parser ELF sendiri dengan `<elf.h>`

Parser sendiri dipilih agar boundary validation dan address mapping dapat dikontrol secara eksplisit. Hal ini penting karena binary diperlakukan sebagai input tidak tepercaya. Approach ini juga memenuhi requirement bahwa aplikasi tidak menyerahkan seluruh proses analisis kepada decompiler eksternal.

Trade-off-nya adalah format yang didukung sengaja dibatasi pada ELF64 x86-64 dan parser harus menangani extended header count, malformed table, serta overflow secara manual.

### Capstone hanya sebagai decoder

Menulis decoder x86-64 lengkap akan menghabiskan sebagian besar waktu proyek dan tidak meningkatkan kualitas algoritma decompiler. Capstone dipilih untuk mendapatkan instruction boundary dan operand terstruktur yang stabil. Capstone tidak dipakai untuk CFG, IR, data-flow, atau pseudocode; bagian tersebut tetap merupakan implementasi proyek.

### Discovery berlapis, bukan hanya symbol table

Mengandalkan `.symtab` akan gagal pada stripped binary. Karena itu discovery menggabungkan symbol, entry point, recursive direct-call target, dan heuristic prologue/alignment. Approach berlapis memberi hasil kuat ketika symbol tersedia, tetapi tetap menghasilkan kandidat masuk akal ketika metadata sudah dihapus.

Heuristik dibuat konservatif karena terlalu agresif memindai setiap byte sebagai kemungkinan prologue akan menghasilkan banyak false positive.

### IR sebelum pseudocode

Menghasilkan C++ langsung dari mnemonic text akan mengikat pseudocode pada formatting Capstone dan sulit melakukan propagation. IR dipakai sebagai lapisan pemisah agar register alias, stack value, call, comparison, dan operation arithmetic memiliki representasi seragam.

IR juga menyediakan fallback `Unknown`, sehingga penambahan dukungan instruction baru tidak memerlukan perubahan parser ELF, CFG, atau GUI.

### Data-flow lokal dan materialisasi pada multi-block CFG

Propagation lokal dipilih karena cukup untuk sample utama `-O1` dan lebih mudah dijaga kebenarannya daripada langsung menerapkan SSA global. Ketika hanya ada satu block, expression dapat digabung secara agresif. Ketika ada beberapa branch, register dimaterialisasi menjadi variable agar nilai dari jalur berbeda tidak tercampur.

Pendekatan ini mengorbankan sebagian kerapian pseudocode, tetapi mengurangi risiko menghasilkan expression yang salah.

### Recovery khusus `setcc` dan `cmovcc`

Saat generated sample resmi dikompilasi dengan GCC `-O1`, `simple_if` berubah menjadi `cmp` + `setg`, sedangkan `absolute_value` berubah menjadi `neg` + `cmovs`. CFG biasa tidak melihat branch pada kedua pola itu.

Solusinya adalah menambahkan IR `Negate` dan `ConditionalSelect`, mempertahankan comparison state, lalu mengeluarkan conditional assignment. Approach ini dipilih karena memperbaiki pola compiler yang benar-benar muncul pada test, bukan mengubah compile flag untuk memaksa compiler menghasilkan branch.

### Structured recovery dengan fallback yang konservatif

Join CFG dan back edge dipakai untuk mengenali struktur umum. Saat pola tidak meyakinkan, generator memilih label/`goto` atau comment unresolved. Decompiler sederhana sebaiknya menunjukkan ketidakpastian daripada menyajikan pseudocode salah seolah-olah pasti benar.

### Cache seluruh hasil per fungsi

Analisis setiap fungsi dilakukan sekali ketika binary dibuka. Selection, search, dan navigation membaca cache. Approach ini menggunakan lebih banyak memori daripada lazy analysis, tetapi membuat interaksi GUI instan dan mencegah full analysis berulang.

### Worker analysis untuk responsiveness

ELF parsing dan lifting tidak boleh menyentuh widget. Menjalankannya di worker memisahkan pekerjaan CPU dari GUI thread. Event non-input tetap diproses agar window dapat repaint dan progress indicator tetap hidup; input ditahan untuk mencegah dua analysis session saling menimpa.

### Qt Graphics View untuk Call Graph

Call Graph menggunakan Qt Graphics View agar node, edge, zoom, pan, selection, dan event navigasi dapat terintegrasi langsung dengan Qt Widgets tanpa proses Graphviz eksternal. Layout engine menerima ukuran masing-masing node karena setiap node menampilkan seluruh instruction assembly dan tingginya tidak seragam.

### Patch panjang tetap

Binary patching membatasi replacement bytes agar sama panjang dengan instruction asli. Mendukung perubahan panjang memerlukan relayout section, pembaruan relative branch, relocation, symbol, dan program-header offset. Pembatasan panjang tetap menjaga address lain stabil dan membuat hasil patch lebih aman untuk fase bonus ini.

## Generated test case dan hasil pengujian

### Menghasilkan binary test

Semua source test berada pada `tests/samples/`. Untuk menghasilkan binary test mandiri:

```bash
./scripts/build_test_binaries.sh
```

Output default:

```text
build/test-binaries/
├── arithmetic
├── simple-if
├── if-else
├── simple-loop
├── function-call
├── nested-condition
├── recursion
└── strings
```

Direktori output dapat ditentukan sendiri:

```bash
./scripts/build_test_binaries.sh /tmp/decompiler-samples
```

Script menggunakan konfigurasi utama:

```text
-std=c++20 -O1 -fno-inline -fno-omit-frame-pointer
-fno-optimize-sibling-calls -no-pie
```

CTest juga menghasilkan sample `stack_local`, `branching`, serta PIE `-O2` dan `-O3` selama proses build.

### Daftar generated sample

| Source | Konstruksi yang diuji | Ekspektasi utama |
|---|---|---|
| `arithmetic.cpp` | dua parameter, addition, direct call | `arg0 + arg1`, call `add(2, 3)`, return |
| `simple_if.cpp` | simple `if` yang menjadi `setg` pada GCC `-O1` | comparison dan conditional recovery |
| `if_else.cpp` | absolute value yang menjadi `neg` + `cmovs` | `if/else` branchless recovery |
| `simple_loop.cpp` | loop dan accumulator | back edge serta loop/fallback jujur |
| `function_call.cpp` | nested direct function call | callee, argument, dan return propagation |
| `nested_condition.cpp` | dua kondisi berurutan | conditional structure dan return |
| `recursion.cpp` | recursive factorial | recursive direct call tanpa infinite analysis |
| `strings.cpp` | RIP-relative read-only string | literal `"hello"` muncul pada pseudocode |
| `stack_local.cpp` | local berbasis `rbp` | nama `local_4` dan load/store stack |
| `branching.cpp` | call pada kedua branch | structured `if/else` dan navigation target |
| `optimized_pie.cpp` | PIE `-O2` dan `-O3` | RIP-relative address dan optimized analysis tidak crash |

Selain binary compiler-generated, `ElfFixture.hpp` membangun ELF sintetis untuk menguji invalid magic, ELF32, endianness salah, machine salah, truncated header, malformed range, missing `.text`, stripped direct-call, dan heuristic function discovery. Unit test disassembler/IR juga memakai byte x86 terkontrol agar classification dan lifting dapat diperiksa secara deterministik.

### Menjalankan pengujian

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Hasil validasi terakhir pada workspace ini (5 Agustus 2026):

```text
100% tests passed, 0 tests failed out of 22
Total Test time (real) = 1.36 sec
```

Rincian test:

| Test | Cakupan | Hasil |
|---|---|---|
| `bootstrap_smoke` | startup Qt dan widget utama | Passed |
| `elf_loader` | validasi ELF, section, symbol, mapping, invalid input | Passed |
| `main_window_phase2` | metadata GUI dan reset setelah load gagal | Passed |
| `pseudocode_view` | read-only view, line number, call activation | Passed |
| `disassembler` | decoding, classification, direct target, invalid byte | Passed |
| `register_abi_ir` | alias register, ABI, stack, IR, unknown fallback | Passed |
| `pseudocode` | arithmetic, call, if/else, unsupported marker | Passed |
| `call_graph` | internal/external/recursive relation | Passed |
| `call_graph_view` | node/edge, zoom, navigation, dynamic node height | Passed |
| `basic_block_cfg` | leader, edge, reachability, dominator, loop | Passed |
| `analysis_phase3` | compiler ELF, stripped call, heuristic discovery | Passed |
| `main_window_phase3` | function search dan assembly navigation | Passed |
| `specification_acceptance` | tujuh sample wajib dan string literal | Passed |
| `cfg_integration` | CFG dari compiler-generated loop | Passed |
| `ir_integration` | IR arithmetic dan stack-local | Passed |
| `pseudocode_integration` | arithmetic, loop, local, branching | Passed |
| `main_window_phase6` | pseudocode presentation | Passed |
| `main_window_workspace` | layout tiga kolom, Symbol Tree, assembly graph | Passed |
| `main_window_phase7` | history, search, worker state, binary kedua | Passed |
| `optimized_pie` | PIE `-O2` dan `-O3` | Passed |
| `binary_patcher` | validation, NOP, output, permission, re-analysis | Passed |
| `main_window_phase8` | bonus integration pada GUI | Passed |

Passing test menunjukkan acceptance path yang tercantum bekerja pada sample yang disediakan. Hasil ini bukan klaim bahwa seluruh kemungkinan binary x86-64 dapat didekompilasi sempurna.

## Implementasi fitur bonus

### Bonus: Call Graph

`CallGraph` dibangun dari direct call pada cache instruction. Internal target menunjuk fungsi yang ditemukan; target lain menjadi external node. Pair caller/callee dideduplicasi dan graph tidak dibangun menggunakan recursive rendering, sehingga self-recursion atau mutual recursion tidak membuat infinite loop.

View menyediakan:

- node untuk setiap fungsi;
- seluruh instruction assembly pada masing-masing internal node, tanpa batas enam baris;
- tinggi node dinamis berdasarkan jumlah instruction;
- edge berwarna sebagai penanda relasi call, sementara isi node memakai warna netral;
- zoom in/out, wheel zoom, pan, Fit All, Fit Selection, dan reset zoom;
- minimap/overview dan isolated-component table;
- highlight fungsi aktif;
- single-click node untuk membuka fungsi serta memilih baris deklarasinya pada Pseudocode View;
- layout memisahkan connected component dan memakai traversal dengan visited-set agar recursive component tetap aman;
- routing edge dan anchor yang mengikuti ukuran aktual node.

Call Graph direfresh setiap session berubah, termasuk setelah patch mengubah direct-call target.

### Bonus: PIE dan optimized binary

Loader menerima `ET_DYN` hanya jika file teridentifikasi sebagai PIE executable melalui interpreter atau `DF_1_PIE`; shared library biasa tetap ditolak.

IR lifter menghitung RIP-relative target menggunakan:

```text
next_instruction_address + signed_displacement
```

Perhitungan memeriksa overflow/underflow. Function discovery tidak sepenuhnya bergantung pada frame prologue klasik, sementara `endbr` diperlakukan sebagai no-op analysis. Test membangun sample PIE menggunakan `-O2` dan `-O3` untuk memastikan loader, discovery, disassembly, RIP-relative lifting, pseudocode, dan Call Graph tidak crash.

Dukungan optimized binary bersifat eksperimental. Register allocation, vectorization, block reordering, dan transformasi kompleks masih dapat menghasilkan fallback.

### Bonus: binary patching

Pengguna memilih instruction pada Assembly View lalu dapat:

- melihat address dan bytes asli;
- memasukkan replacement hex;
- mengisi seluruh instruction dengan NOP;
- memilih file output;
- mengonfirmasi overwrite jika target sudah ada.

Sebelum menulis file, `BinaryPatcher` memvalidasi:

1. session memiliki ELF valid;
2. patch tidak kosong;
3. panjang replacement sama dengan instruction asli;
4. address dan seluruh patch berada di executable section;
5. replacement dapat didekode sebagai instruction sequence valid;
6. address dapat dipetakan ke file offset;
7. bytes pada file masih sama dengan bytes yang dianalisis;
8. file asli tidak ditimpa tanpa konfirmasi.

Output mempertahankan permission file sumber. Setelah patch berhasil, GUI membuka output sebagai session baru, menjalankan disassembly/CFG/IR/pseudocode ulang, dan merefresh Call Graph. Default path memakai suffix `-patched`, sehingga file asli tidak berubah.