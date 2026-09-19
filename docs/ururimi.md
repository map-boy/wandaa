# Ururimi rwa Wandaa — Language Reference

*Iyi nyandiko isobanura ururimi rwa Wandaa mu Kinyarwanda no mu Cyongereza.*
*This document specifies the Wandaa language in Kinyarwanda and English.*

---

## 1. Ibanze — Basics

Porogaramu ya Wandaa ni urutonde rw'amabwiriza. Buri ibwiriza rirangira na `;`.

A Wandaa program is a sequence of statements. Each statement ends with `;`.

```wandaa
andika("Mwiriwe Rwanda");
```

Ibisobanuro bitangirwa na `#` — comments start with `#` and run to end of line.

```wandaa
# Iki ni igisobanuro.
andika(1);   # n'iki na cyo
```

---

## 2. Amagambo y'ingenzi — Keywords

| Wandaa | Icyo bisobanura | English |
|---|---|---|
| `reka` | gutangiza ikigereranyo | declare a variable |
| `niba` | niba ari byo | `if` |
| `ubundi` | ubundi | `else` |
| `mugihe` | mugihe | `while` |
| `umurimo` | umurimo | function |
| `tanga` | tanga agaciro | `return` |
| `andika` | andika ku cyapa | print |
| `nibyo` / `oya` | nibyo / oya | `true` / `false` |
| `na` / `cyangwa` / `si` | na / cyangwa / si | `and` / `or` / `not` |
| `hagarika` / `komeza` | hagarika / komeza | `break` / `continue` |
| `hanze` | umurimo wo hanze (DLL) | external DLL function |
| `injiza` | kwinjiza indi dosiye | import a module |

---

## 3. Ibigereranyo — Variables

```wandaa
reka x = 10;
reka izina = "Mugisha";
x = x + 1;
```

Ibigereranyo bifite ubwoko bune: **umubare** (integer), **umubare w'ibice**
(f64 float), **ijambo** (string), na **urutonde** (array). Ubwoko buhita
bumenyekana — types are inferred, not declared.

---

## 4. Imibare n'ibipimo — Operators

| Ikimenyetso | Icyo gikora |
|---|---|
| `+` `-` `*` `/` | imibare (arithmetic); `+` also concatenates strings |
| `<` `>` `<=` `>=` | kugereranya (comparison) |
| `==` `!=` | kungana / kudahuza (equality), works on numbers and strings |
| `na` / `&&` | `and`, short-circuit |
| `cyangwa` / `\|\|` | `or`, short-circuit |
| `si` / `!` | `not` |
| `&x` | aderesi (address-of), kugira ngo ihabwe umurimo wo hanze |
| `x?` | gutwara ikosa (propagate a failure) — reba igice cya 8b |

Imibare yuzuye ni iy'ibice 64 (signed 64-bit). Imibare y'ibice ni IEEE-754
`f64`.

`/` hagati y'imibare yuzuye ni igabana ry'imibare yuzuye — **integer division**,
truncating toward zero. Iyo kimwe mu bibarwa ari umubare w'ibice, byombi bihita
bihinduka ibice.

**Short circuit.** Muri `a na b`, `b` ntibarwa iyo `a` ari 0. Muri
`a cyangwa b`, `b` ntibarwa iyo `a` itari 0.

---

## 5. Kugenzura — Control flow

```wandaa
niba (x > 10) {
  andika("kinini");
} ubundi niba (x > 5) {
  andika("hagati");
} ubundi {
  andika("gito");
}

reka i = 0;
mugihe (i < 10) {
  niba (i == 3) { komeza; }     # continue
  niba (i > 7)  { hagarika; }   # break
  andika(i);
  i = i + 1;
}
```

---

## 6. Imirimo — Functions

```wandaa
umurimo kubara(a, b) {
  tanga a + b;
}

andika(kubara(3, 4));   # 7
```

- Imirimo ishobora kwihamagara — recursion is supported.
- Nta mubare ntarengwa wa parametero — there is no argument-count limit;
  arguments five and up travel in the caller's stack area per the Win64 ABI.
- Umurimo udatanga agaciro utanga 0 — a function that falls off the end
  returns 0.
- Imirimo ishobora guhamagarwa mbere y'uko isobanurwa — forward references
  are fine, since calls are resolved after the whole program is generated.

---

## 6b. Imibare y'ibice — Floating point

Umubare ufite akadomo ni `f64` (IEEE-754 double). A number written with a
decimal point is an f64.

```wandaa
reka igiciro = 19.99;
reka ingano = 3;
andika(igiciro * ingano);     # 59.97
```

**Igabana — division.** Igabana hagati y'imibare yuzuye ntabwo rihinduka:

| Ibarwa | Igisubizo | Ubwoko |
|---|---|---|
| `7 / 2` | `3` | umubare wuzuye |
| `7 / 2.0` | `3.5` | ibice |
| `7.0 / 2` | `3.5` | ibice |
| `1.5 + 1` | `2.5` | ibice |

Ibi bituma porogaramu zose za kera zikomeza gukora uko zari zisanzwe — existing
programs keep their results.

**Kwandika — printing.** `andika` yandika ibice bitarenze 6, ikuraho zeru zo ku
mpera:

| Agaciro | Byandikwa |
|---|---|
| `3.14` | `3.14` |
| `0.5` | `0.5` |
| `2.0` | `2` |
| `-0.25` | `-0.25` |
| `1.0 / 3.0` | `0.333333` |

Ntabwo ari shortest-round-trip: `0.1 + 0.2` yandika `0.3`, si
`0.30000000000000004`. Iri ni ihitamo ryo kwandika gusa — a formatting choice,
changeable later without a language change.

**Guhindura — conversion.**

```wandaa
andika(mu_bice(7));                  # 7    umubare wuzuye -> ibice
andika(mu_mubare_wuzuye(3.99));      # 3    ibice -> umubare wuzuye
andika(mu_mubare_wuzuye(-3.99));     # -3   icyerekezo cya zeru (toward zero)
```

**NaN na infinity.** `0.0 / 0.0` itanga `NaN`, `1.0 / 0.0` itanga `inf`. NaN
ntihwanye n'ikintu na kimwe, nayo ubwayo irimo — NaN compares false against
everything, including itself:

```wandaa
reka nan = 0.0 / 0.0;
andika(nan == nan);      # 0
andika(nan != nan);      # 1
andika(nan < 1.0);       # 0
```

**Ibitaraboneka — limitations.** Umurimo wa `hanze` usubiza `double` ntabwo
urasomwa neza: Win64 isubiza ibice muri XMM0, naho Wandaa isoma RAX. A `hanze`
function that RETURNS a double is not yet read correctly; passing doubles to one
works.

---

## 6c. Imirimo itagira izina — Closures

`umurimo` idafite izina ni agaciro: ushobora kuyishyira mu kigereranyo,
kuyihereza undi murimo, no kuyisubiza.

An `umurimo` with no name is a value. It can be stored in a variable, passed
to another function, put in an array, and returned:

```wandaa
reka kubyaza = umurimo(x) { tanga x * x; };
andika(kubyaza(7));            # 49

umurimo koresha(g, v) { tanga g(v); }
andika(koresha(kubyaza, 9));   # 81
```

Ifata ibigereranyo byo hanze ikoresha — **it captures the enclosing variables
it uses**, so a function can build another function:

```wandaa
umurimo gukuba(k) { tanga umurimo(x) { tanga x * k; }; }
reka kabiri = gukuba(2);
reka gatatu = gukuba(3);
andika(kabiri(21));            # 42
andika(gatatu(14));            # 42
```

### Ifata ku gaciro — capture is by value

Ifata **kopi** y'agaciro igihe iremwe, ntabwo ifata ikigereranyo ubwacyo:

```wandaa
reka n = 10;
reka f = umurimo(x) { tanga x + n; };
n = 99;
andika(f(5));                  # 15, si 104
```

Iyi ni ihitamo, si ubunebwe. Capture by value is a decision, not a shortcut: a
closure can outlive the call it was created in, and with no reference counting
yet (see [ROADMAP.md](../ROADMAP.md)) a captured *variable* would be a pointer
into a dead frame. A copy is always safe, and it is the rule that is easiest to
explain.

Bivuze ko closure idashobora guhindura ikigereranyo cyo hanze. It also means a
closure cannot mutate an enclosing variable — assigning to a captured name
changes only the closure's own copy.

Uko bikorwa imbere: umurimo utagira izina uba umurimo usanzwe wo hejuru,
hamwe n'agace ka heap gafite aderesi y'amabwiriza na kopi ya buri gaciro
gifashwe. Internally an anonymous function becomes an ordinary top-level
function plus a heap block holding its code pointer and a copy of each captured
value; the block travels as a hidden first argument.

Ibigomba kumenyekana — the limits:

- Closure yakiriwe nka **parameter** (nka `g` muri `koresha` hejuru) ntizwi
  ubwoko bw'ibyo isubiza, bityo isubizwa nk'umubare. A closure received as a
  parameter has no known return type, so a string it returns prints as a
  pointer. Assign it to a variable from the `umurimo` or from the function that
  builds it, and the type is known.
- Nta recursion y'umurimo utagira izina: `reka f = umurimo(n){ ... f(...) ... };`
  ifata `f` mbere y'uko `reka` iyishyiraho, bityo compiler irayanga ikubwira
  gukoresha umurimo ufite izina. An anonymous function cannot call itself: it
  captures the variable before the declaration binds it, so this is rejected at
  compile time with a message pointing at the fix. A named `umurimo` recurses
  normally.

---

## 6d. Ubwoko bwanditswe — Written types

Buri annotation ni **guhitamo**, si itegeko. Aho itabaho, compiler ikomeza
kwikekera ubwoko nk'uko yabigenzaga. Aho ihari, ni yo ifite ijambo rya nyuma.

Every annotation is **optional**. Where one is absent, inference does exactly
what it did before, so every existing `.waa` file keeps compiling and keeps its
meaning. Where one is present it **wins** — inference falls back to `umubare`
for anything it cannot work out, and a written type is how you say that
fallback is wrong.

```wandaa
umurimo hura(a: ijambo, b: ijambo): ijambo { tanga a + b; }
reka izina: ijambo = hura("Wan", "daa");
```

| Ubwoko | Icyo ari cyo |
|---|---|
| `umubare` | umubare wuzuye (64-bit signed integer) |
| `ibice` | umubare w'ibice (f64) |
| `ijambo` | string |
| `urutonde` | array |
| `igisubizo` | result (igice cya 8b) |
| izina ry'ubwoko | ubwoko bwatangajwe na `ubwoko` |

### Ubwoko bufite igipimo — type arguments

`urutonde<ibice>` na `igisubizo<ijambo>` bivuga icyo urutonde rurimo n'icyo
igisubizo gitwaye. `urutonde<ibice>` and `igisubizo<ijambo>` say what an array
holds and what a result carries — which is exactly what inference previously
had to guess:

```wandaa
reka ibipimo: urutonde<ibice> = urutonde(3);
ibipimo[0] = 1.5;
andika(ibipimo[0]);            # 1.5, si imibare y'ibice bya raw

umurimo shakisha(k: umubare): igisubizo<ijambo> {
  niba (k == 0) { tanga byanze("ntabwo ribonetse"); }
  tanga byakunze("ibonetse");
}
andika(agaciro(shakisha(1)) + "!");
```

Ni na ko bikemura aho closure ihawe nka parameter — a written return type is
what makes a closure passed as a *parameter* usable, since otherwise the call
site has no idea what it gives back:

```wandaa
umurimo koresha(g): ijambo { tanga g("Ana"); }
```

### Ubwoko bw'ikigereranyo — naming a record

Compiler ntibasha kumenya ubwoko bw'ikigereranyo gifashwe mu rutonde cyangwa
gihawe umurimo. Ugomba kubwandika:

A record pulled out of an array, received as a parameter, or returned by a
function carries no type the compiler can work out on its own — without a
written one, every field access resolves to offset 0 and silently reads the
wrong bytes. Write it down:

```wandaa
ubwoko Ikimenyetso { ubwoko_bwacyo: ijambo, inyandiko: ijambo, umurongo: umubare }

umurimo erekana(k: Ikimenyetso): ijambo { tanga k.inyandiko; }

reka bose: urutonde<Ikimenyetso> = urutonde(0);
reka k: Ikimenyetso = bose[0];
andika(bose[0].inyandiko);     # `urutonde<Ikimenyetso>` is what resolves this
```

### Mu bwoko — record fields

```wandaa
ubwoko Umuntu { izina: ijambo, imyaka: umubare }
```

Nyuma ya `:`, **umubare ni ubugari bw'ibice** (`family:2`, kugira ngo bihuze na
struct ya C) naho **izina ni ubwoko**. After `:` a number is a byte width and a
name is a type; they never collide, so the packed-record form is unchanged.

### Amakosa afatwa — what this rejects

Ubwoko bwanditswe butuma compiler yanga ibyo yemeraga mbere:

```
igipimo cya 1 cya 'f' gisaba ijambo, cyahawe umubare
```

Ibyangwa ni ibizwi gusa. Only DECLARED types are checked, and only when the
argument's type is certain: a value whose type inference could not work out is
still allowed through, because rejecting on a guess would reject working
programs. An integer passed where `ibice` is declared is promoted, as
everywhere else in the language.

---

## 6e. Ubwoko rusange — Generics

`umurimo mbere<T>(a: urutonde<T>): T` ni umurimo umwe ukorera buri bwoko.

```wandaa
umurimo mbere<T>(a: urutonde<T>): T { tanga a[0]; }

reka amazina: urutonde<ijambo> = ["Ana", "Eric"];
reka ibipimo: urutonde<ibice>  = [1.5, 2.5];

andika(mbere(amazina) + "!");   # Ana!  -- ni ijambo
andika(mbere(ibipimo));         # 1.5   -- ni ibice
```

`T` ihabwa agaciro kuri buri hamagara, ihereye ku bipimo yahawe. `T` is bound
at each call site from the arguments it was given.

**Nta monomorphisation.** Buri gaciro muri Wandaa ni bayiti 8 muri register,
bityo **umubiri umwe** ukorera buri `T` — nta kopi y'umurimo ikorwa. Generics
need no monomorphisation: every Wandaa value is 8 bytes in a register, so one
body serves every `T` and no copy of the function is generated. That is why
this costs nothing at runtime, and it is the reason generics fit a compiler
that is meant to stay small (GOVERNANCE principle 3).

Ushobora gukoresha ubwoko burenze bumwe, no kuvanga na bwa bundi buzwi:

```wandaa
umurimo iya_mbere<A, B>(x: A, y: B): A { tanga x; }
umurimo subiramo<T>(x: T, n: umubare): T { tanga x; }
```

Igisubizo na rwo:

```wandaa
umurimo cyangwabyo<T>(r: igisubizo<T>, d: T): T {
  niba (byarakunze(r)) { tanga agaciro(r); }
  tanga d;
}
```

Ibigomba kumenyekana — the limits:

- Mu mubiri w'umurimo, `T` ni **impfabusa**: compiler ntizi icyo ari cyo, kandi
  ni byo bikwiye. Inside the body `T` is opaque — the compiler does not know
  what it is, and the code there only moves 8 bytes around. So `x + 1` on a `T`
  is integer arithmetic whatever the caller passed; a generic body cannot do
  arithmetic or concatenation that depends on `T`.
- `T` ifatwa ku gipimo cya mbere kiyivuga. `T` is bound from the first argument
  that mentions it, so a call whose arguments disagree takes the earliest
  rather than reporting a conflict.
- Nta bwoko rusange kuri `ubwoko` (records) ubu. Generic *records* are not
  supported yet — only functions. See [ROADMAP.md](../ROADMAP.md).

---

## 7. Amagambo — Strings

Ijambo rya Wandaa ni aderesi y'ibice byaryo, rifite **uburebure bw'ibice 8
imbere yaryo**. Ni yo mpamvu `uburebure()` ari igikorwa cy'igihe kimwe, kandi
ni yo mpamvu ijambo rishobora guhita rijya muri DLL ya Windows: rirangirira
kuri `\0` nka C.

A Wandaa string is a pointer to its bytes with an **8-byte length header
immediately before them**. That makes `uburebure()` O(1), and because the bytes
are also NUL-terminated a Wandaa string can be handed straight to a Windows
`...A` function.

Escapes: `\n` `\t` `\r` `\0` `\\` `\"`.

```wandaa
reka a = "Mwiriwe";
reka b = a + " Rwanda";        # concatenation
andika(uburebure(b));           # 14
andika(inyuguti(b, 0));         # 77  (ASCII 'M')
andika(igice(b, 8, 6));         # "Rwanda"
andika(a == "Mwiriwe");         # 1
```

---

## 8. Intonde — Arrays

```wandaa
reka a = [10, 20, 30];
andika(a[0]);            # 10
a[1] = 99;
andika(ubunini(a));      # 3

reka b = urutonde(100);  # 100 elements, all 0
b[99] = 7;
```

Urutonde rubikwa muri heap, rufite **umubare w'ibice 8 imbere** — arrays are
heap allocated with an 8-byte element count before the data, the same shape as
strings.

### Kongeraho — growing an array

`ongeraho(a, x)` yongeraho `x` **igasubiza** urutonde rwo gukoresha guhera
ubu, kuko kwagura bishobora kurwimura:

`ongeraho(a, x)` appends and **returns** the array to use from now on, because
growing it may move it. Always write the assignment:

```wandaa
reka a = urutonde(0);
reka i = 0;
mugihe (i < 10) { a = ongeraho(a, i * i); i = i + 1; }
andika(ubunini(a));            # 10
```

Ubunini bwiyongera kabiri buri gihe, bityo kongeraho n'inshuro bisaba akazi ka
O(n) muri rusange, si O(n²). Capacity doubles, so n appends cost O(n) copying
in total.

`ubunini()` ni **umubare w'ibice bikoreshwa**, si ubushobozi bw'agace:
`ubunini` is the COUNT in use, never the capacity, and bounds checks use that
same count — a block that can hold four but holds one rejects `a[1]`.

Umwanya wa kera ntusohorwa igihe urutonde rwaguwe: ikindi kigereranyo gishobora
kuba kikirwerekeza. The old block is not freed when the array grows, because
another variable may still point at it; at most one extra copy is left behind.
Urutonde rwubatswe na `ongeraho` ntirusohorwa mu buryo bwikora (reba igice 11b).

---

Indexing **is** bounds-checked, ku gusoma no ku kwandika — on reads and on
writes alike. Niba index isohotse mu rutonde, porogaramu ihagarara ivuga
umurongo, index, n'ubunini:

```
Ikosa: urutonde rurenzwe (index out of range) ku murongo: 13, aho ugerageje: 3, ubunini: 3
```

Porogaramu isohoka na code 1. Index mbi (negative) na yo ifatwa kimwe: igereranya
rikorwa nka *unsigned*, bityo -1 ihinduka umubare munini cyane kandi inanirwa
igeragezwa rimwe. `inyuguti()` yo ntihagarika porogaramu — igarura -1.

---

## 8b. Igisubizo — Results

Umurimo ushobora kunanirwa ntugomba guhitamo hagati yo guhagarika porogaramu
no kubeshya. Utanga **igisubizo**: byagenze neza gifite agaciro, cyangwa
byanze gifite ubutumwa.

A function that can fail should not have to choose between crashing and
lying about its answer. It returns a **result** instead: either a success
carrying a value, or a failure carrying a message.

```wandaa
umurimo gabanya(a, b) {
  niba (b == 0) { tanga byanze("ntushobora kugabanya na zeru"); }
  tanga byakunze(a / b);
}

reka r = gabanya(20, 4);
niba (byarakunze(r)) { andika(agaciro(r)); }
ubundi { andika(ikosa(r)); }
```

| Umurimo | Icyo ukora |
|---|---|
| `byakunze(v)` | igisubizo cyagenze neza gifite `v` — a success carrying `v` |
| `byanze(ubutumwa)` | igisubizo cyanze gifite ubutumwa — a failure carrying a message |
| `byarakunze(r)` | 1 iyo byagenze neza, 0 iyo byanze |
| `agaciro(r)` | agaciro kiri imbere — the value inside |
| `ikosa(r)` | ubutumwa bw'ikosa — the failure message |

### `?` — gutwara ikosa

`?` ni ikimenyetso cy'ingenzi. Iyo byagenze neza, ni agaciro kiri imbere.
Iyo byanze, umurimo urimo uhita utanga iryo kosa nk'uko riri.

`?` is the point of the type. On a success it is the value inside; on a
failure the enclosing function returns that failure immediately. A chain of
fallible calls reads like a chain of ordinary ones, and no error can be
dropped by forgetting to look at it:

```wandaa
umurimo kabiri(a, b) {
  reka k = gabanya(a, b)?;      # ihagarara hano iyo gabanya yanze
  tanga byakunze(k * 2);
}
```

Ku rwego rwo hejuru nta murimo uhamagaye, bityo `?` yandika ubutumwa
igasohoka na 1. At top level there is no caller to return to, so `?` reports
the message and exits 1 rather than losing it.

Umurongo werekanwa ni aho ikosa **ryavukiye** — aho `byanze(...)` yakoreshejwe,
si aho `?` iri. The line reported is where the failure was *created*, not where
it was propagated, because that is the line worth looking at.

Ibigomba kumenyekana — the limits, and they are real:

- `agaciro(r)` ku gisubizo cyanze ihagarika porogaramu, nka `unwrap` muri Rust.
  Koresha `byarakunze()` cyangwa `?` mbere. `ikosa(r)` ku gisubizo cyagenze
  neza na yo ihagarika, kuko ari ikosa ryo kwandika.
- Nta generics (reba [ROADMAP.md](../ROADMAP.md)), bityo ubwoko bw'agaciro
  buvamo *inference* aho kuva mu bisobanuro. Iyo compiler idashobora kumenya
  ubwoko, isubiza `Int`. There are no generics yet, so the payload's type is
  inferred rather than declared, and falls back to integer when it cannot be
  worked out — a string payload would then print as a pointer.
- `andika(r)` ku gisubizo cyagenze neza cyandika `byakunze` gusa, kitandika
  agaciro: nta kintu na kimwe mu gihe cyo gukora kivuga icyo ayo mabayiti 8
  ari cyo. `andika` on a success prints only `byakunze`, because nothing at
  runtime records what the payload is.
- Umurimo uhamagara undi wanditswe **nyuma** muri dosiye ntabwo umenya
  ubwoko bw'agaciro ke — ni ko bimeze na `urutonde`.

---

## 8c. Inkoranya — Maps

Inkoranya ihuza **ijambo** n'agaciro. A map from string keys to values:

```wandaa
reka m = inkoranya();
m = shyiramo(m, "izina", 42);
andika(fata(m, "izina"));      # 42
andika(ubunini(m));            # 1
```

`shyiramo` isubiza inkoranya yo gukoresha guhera ubu, nka `ongeraho`, kuko
kwagura biyimura. `shyiramo` returns the map to use from now on, exactly like
`ongeraho`, because growing it moves it. Always write the assignment.

| Umurimo | Icyo ukora |
|---|---|
| `inkoranya()` | inkoranya nshya, irimo ubusa |
| `shyiramo(m, k, v)` | shyiramo `v` kuri `k`, isubize inkoranya |
| `fata(m, k)` | agaciro kari kuri `k`, cyangwa 0 iyo kadahari |
| `arimo(m, k)` | 1 iyo `k` ihari, 0 iyo itahari |
| `ubunini(m)` | umubare w'ibyashyizwemo |

`fata` isubiza 0 iyo urufunguzo rutabonetse, bityo koresha `arimo` kugira ngo
umenye itandukaniro n'agaciro ari 0 koko. `fata` returns 0 for a missing key,
so use `arimo` to tell that apart from a value that really is 0:

```wandaa
m = shyiramo(m, "zeru", 0);
andika(fata(m, "zeru"));       # 0
andika(arimo(m, "zeru"));      # 1
```

Uko bikorwa imbere: open addressing, linear probing, FNV-1a ku bice by'ijambo,
n'umubare w'utwobo ari imbaraga za 2. Internally: open addressing with linear
probing, FNV-1a over the key bytes, and a power-of-two bucket count so the
modulo is a bitwise AND. It rehashes into twice as many buckets at a load
factor of one half, and as with `ongeraho` the old table is not freed.

---

## 8d. Ibikorwa ku biti — Bitwise operations

Ururimi rukoreshwa mu kwandika sisitemu rukeneye gukora ku biti: imiterere ya
dosiye ya binary, hash, cyangwa encoder y'amabwiriza.

| Umurimo | Icyo ukora |
|---|---|
| `biti_na(a, b)` | AND ku biti byose 64 |
| `biti_cyangwa(a, b)` | OR |
| `biti_gutandukana(a, b)` | XOR |
| `biti_ibumoso(a, n)` | kwimura ibumoso (shift left) |
| `biti_iburyo(a, n)` | kwimura iburyo (shift right) |

```wandaa
umurimo modrm(md: umubare, reg: umubare, rm: umubare): umubare {
  tanga biti_cyangwa(biti_ibumoso(md, 6),
         biti_cyangwa(biti_ibumoso(biti_na(reg, 7), 3), biti_na(rm, 7)));
}
andika(modrm(3, 2, 3));        # 211
```

Ni ibikorwa fatizo, si ibimenyetso, kubera impamvu imwe isobanutse: `>>`
ntibishoboka kuyibona nk'ikimenyetso kimwe hatangijwe ikibazo muri
`urutonde<igisubizo<umubare>>`, aho `>>` ari ibimenyetso bibiri. These are
builtins rather than operators for one concrete reason: `>>` cannot be lexed
as a single token without breaking `urutonde<igisubizo<umubare>>`, where the
two closing angles are two separate tokens. One consistent spelling beats a
mix of operators and calls.

---

## 9. Ibikorwa fatizo — Builtins

| Umurimo | Icyo ukora |
|---|---|
| `andika(...)` | andika ku cyapa (print, one line per argument) |
| `uburebure(s)` / `ubunini(a)` | length of a string / array |
| `inyuguti(s, i)` | ASCII code at index i, or -1 if out of range |
| `igice(s, aho, ingano)` | substring, clamped to the string |
| `mu_ijambo(n)` | number → string |
| `mu_mubare(s)` | string → number (0 if unparseable) |
| `mu_bice(n)` | integer → f64 |
| `mu_mubare_wuzuye(x)` | f64 → integer, truncating toward zero |
| `urutonde(n)` | new zero-filled array of n elements |
| `ongeraho(a, x)` | append x, returning the array to keep — reba igice cya 8 |
| `inkoranya()` / `shyiramo` / `fata` / `arimo` | inkoranya (map) — reba igice cya 8c |
| `biti_na` / `biti_cyangwa` / `biti_gutandukana` / `biti_ibumoso` / `biti_iburyo` | ibikorwa ku biti — reba igice cya 8d |
| `ijambo(p)` | raw NUL-terminated pointer → Wandaa string |
| `soma(dosiye)` | read a whole file as a string |
| `andikamo(dosiye, ibirimo)` | write a string to a file |
| `byakunze(v)` / `byanze(u)` | igisubizo (result) — reba igice cya 8b |
| `byarakunze(r)` / `agaciro(r)` / `ikosa(r)` | kugenzura igisubizo |

---

## 10. Guhamagara Windows — FFI

`hanze` itangaza umurimo uri muri DLL. Wandaa iwushyira mu import table
y'executable, ihamagare inyuze mu IAT.

`hanze` declares a function living in a DLL. The compiler puts it in the
executable's import table and calls it through the IAT — no wrapper, no glue.

```wandaa
hanze "kernel32.dll" Sleep(ms);
hanze "user32.dll" MessageBoxA(hwnd, ubutumwa, umutwe, ubwoko);

Sleep(1000);
MessageBoxA(0, "Mwiriwe", "Wandaa", 0);
```

Ingingo z'ingenzi — things to know:

- Amagambo ya Wandaa arangirira kuri `\0`, bityo ajya muri `...A` functions.
- Umurimo utanga `char*` ukeneye `ijambo()` kugira ngo ube ijambo rya Wandaa —
  a returned `char*` has no length header, so wrap it in `ijambo()`.
- Ibi ni byo bituma Wandaa igera kuri **database** (sqlite3.dll),
  **serveri** (ws2_32.dll), na **AI/ML** (onnxruntime.dll) — nta kintu na kimwe
  gikenewe mu compiler.

---

## 11. Amamodule — Modules

```wandaa
injiza "imibare.waa";
injiza "amagambo.waa";

andika(mugabane(48, 18));   # 6
```

Aho dosiye ishakirwa — search order:

1. Hafi ya dosiye iyinjiza (relative to the importing file)
2. `WANDAA_PATH`
3. `lib/` (isomero rusange — the standard library)

Buri module yinjizwa rimwe gusa, kandi uruziga rutera ikosa — each module is
included once, and an import cycle is an error.

---

## 11b. Kugarura umwanya — Memory

Wandaa ntisaba gusohora umwanya wa heap. Compiler isohora ibyo ishobora
KWEMEZA ko byapfuye; ibindi bisigara.

Wandaa has no free. The compiler releases what it can **prove** is dead, and
leaves everything else alone.

A local is released when both hold:

- buri gaciro cyayo cyari **umwanya mushya** (`urutonde(n)`, `[...]`, guhuza
  amagambo, `byakunze(...)`, umurimo utagira izina) -- every value it held was
  a fresh allocation, never a string literal and never another function's
  return value;
- kandi ntiyigeze isohoka -- the value never left the variable: not assigned to
  another name, not returned, not put in an array, a record, a result or a
  closure, and not passed to anything that might keep it.

Iyo byemejwe, isohorwa iyo isimbuwe no ku musozo w'umurimo. Bivuze ko loop
isubiramo umwanya umwe aho kugenda yiyongera:

```wandaa
reka i = 0;
mugihe (i < 100000) {
  reka ubutumwa = "umukiriya " + mu_ijambo(i);   # isohorwa buri gihe
  andikamo("log.txt", ubutumwa);
  i = i + 1;
}
```

Ibigomba kumenyekana — what this does NOT do, stated plainly:

- **Nta reference counting.** Iyo compiler idashobora kwemeza, ntisohora --
  bityo ikibazo ni ko umwanya usigara, si ukoresha umwanya wasohowe. If the
  compiler cannot prove it, nothing is freed. The failure mode is a leak, never
  a use-after-free, and that is deliberate: full refcounting would have to know
  which of a block's 8-byte slots are pointers, and a slot whose type was never
  worked out would be decremented as if it were one.
- **Nta kwinjira mu bice.** Gusohora urutonde ntibisohora ibirimo, bityo
  urutonde rw'amagambo rusiga amagambo. Freeing a block does not free what it
  contains, so an array of strings leaves the strings behind. That is also why
  an element read out of an array stays valid after the array is gone.
- Ibi biri muri [ROADMAP.md](../ROADMAP.md) nk'intambwe ya mbere.

---

## 12. Amakosa — Errors

Amakosa yo mu gihe cyo gukora (division by zero, bad memory access) afatwa na
vectored exception handler ihita yandika umurongo w'inkomoko:

Runtime faults are caught by a vectored exception handler that reports the
source line instead of a raw exit code:

```
Ikosa ku murongo: 3
```

Hari ahandi porogaramu ihagarara ivuga umurongo — three other stops report a
line the same way:

- `a[i]` iyo `i` isohotse mu rutonde (reba igice cya 8)
- `agaciro(r)` ku gisubizo cyanze, na `ikosa(r)` ku gisubizo cyagenze neza
- `?` ku rwego rwo hejuru iyo byanze (reba igice cya 8b)

---

## 13. Ibitaraboneka — Not yet in the language

Ibi biri muri [ROADMAP.md](../ROADMAP.md):

- `for` loops
- Ubwoko rusange kuri `ubwoko` (generic records; generic *functions* work —
  reba igice cya 6e)
- Amagambo ya Unicode arenze ASCII mu `inyuguti()` / `igice()` (byombi
  bikorera ku bice, not on code points)
