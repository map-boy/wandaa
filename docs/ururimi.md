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
| `ijambo(p)` | raw NUL-terminated pointer → Wandaa string |
| `soma(dosiye)` | read a whole file as a string |
| `andikamo(dosiye, ibirimo)` | write a string to a file |

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

## 12. Amakosa — Errors

Amakosa yo mu gihe cyo gukora (division by zero, bad memory access) afatwa na
vectored exception handler ihita yandika umurongo w'inkomoko:

Runtime faults are caught by a vectored exception handler that reports the
source line instead of a raw exit code:

```
Ikosa ku murongo: 3
```

---

## 13. Ibitaraboneka — Not yet in the language

Ibi biri muri [ROADMAP.md](../ROADMAP.md):

- `struct` / records
- `for` loops
- Ubwoko bwanditswe (explicit type annotations)
- Amagambo ya Unicode arenze ASCII mu `inyuguti()` / `igice()` (byombi
  bikorera ku bice, not on code points)
