# Isomero rusange — Standard Library Reference

*Iyi nyandiko ikurwa mu masomero ubwayo. This page is generated from the
sources in `lib/` by `tools/gen_stdlib_docs.py`; edit the `.waa` files, not
this file.*

Kwinjiza module — to use a module:

```wandaa
injiza "imibare.waa";
```


---

## `imibare.waa` — Imibare — Arithmetic

**`nyacyo(n)`**  
Agaciro nyacyo -- absolute value.

**`ntoya(a, b)`**  
Agaciro gato -- the smaller of two numbers.

**`nini(a, b)`**  
Agaciro kanini -- the larger of two numbers.

**`hagati(n, ntoyaa, ninii)`**  
Kugarura agaciro hagati ya ntoya na nini -- clamp.

**`ingufu(a, b)`**  
a ku ngufu za b -- a raised to the power b (b >= 0).

**`igisigara(a, b)`**  
Igisigara -- remainder of a divided by b.

**`ni_gicuri(n)`**  
Ni umubare utagabanywa na 2 -- true when n is even.

**`mugabane(a, b)`**  
Umugabane munini usangiwe -- greatest common divisor.

**`gusangira(a, b)`**  
Umubare uto usangiwe -- least common multiple.

**`umuzi(n)`**  
Umuzi wa kabiri -- integer square root (floor).

---

## `amagambo.waa` — Amagambo — Strings

**`aho_iri(s, gito)`**  
Aho ijambo rito riri mu ijambo rinini -- index of `gito` in `s`, or -1.

**`arimo(s, gito)`**  
Irimo -- true when `s` contains `gito`.

**`gutangirana(s, intangiriro)`**  
Gutangirana -- true when `s` starts with `intangiriro`.

**`kurangirana(s, impera)`**  
Kurangirana -- true when `s` ends with `impera`.

**`hejuru(s)`**  
Inyuguti nkuru -- ASCII upper case.

**`hasi(s)`**  
Inyuguti nto -- ASCII lower case.

**`inyuguti_ijambo(c)`**  
Guhindura kode ya ASCII mu ijambo ry'inyuguti imwe -- one-character string. Yubakiye ku ijambo rifite inyuguti zose za ASCII zikoreshwa.

**`gusubiramo(s, n)`**  
Gusubiramo ijambo inshuro n -- repeat.

**`ni_umwanya(c)`**  
Ni umwanya -- space, tab, newline or carriage return.

**`gukuraho_umwanya(s)`**  
Gukuraho umwanya ku mpera zombi -- trim. Wandaa nta `break` ifite, ni yo mpamvu dukoresha ikimenyetso `birangiye`.

**`gusimbura(s, gito, gishya)`**  
Gusimbura ijambo rito ryose -- replace every occurrence.

---

## `urutonde.waa` — Urutonde — Lists

**`igiteranyo(a)`**  
Igiteranyo cy'ibiri ku rutonde -- sum.

**`ntoya_muri(a)`**  
Agaciro gato ku rutonde -- minimum.

**`nini_muri(a)`**  
Agaciro kanini ku rutonde -- maximum.

**`aho_kari(a, x)`**  
Aho agaciro kari ku rutonde -- index of, or -1.

**`rurimo(a, x)`**  
Rurimo -- true when the list contains x.

**`gukoporora(a)`**  
Kopi y'urutonde -- a copy.

**`guhindura_icyerekezo(a)`**  
Guhindura icyerekezo -- reversed copy.

**`gushyira_ku_murongo(a)`**  
Gushyira ku murongo -- sorted copy (insertion sort; the lists Wandaa programs handle today are small, and this keeps the code readable).

---

## `sisitemu.waa` — Sisitemu — Windows system calls

**`Sleep(ms)`** — umurimo wo hanze muri `kernel32.dll` (external, from `kernel32.dll`)

**`GetTickCount()`** — umurimo wo hanze muri `kernel32.dll` (external, from `kernel32.dll`)

**`GetCurrentProcessId()`** — umurimo wo hanze muri `kernel32.dll` (external, from `kernel32.dll`)

**`GetCommandLineA()`** — umurimo wo hanze muri `kernel32.dll` (external, from `kernel32.dll`)

**`GetLastError()`** — umurimo wo hanze muri `kernel32.dll` (external, from `kernel32.dll`)

**`CreateDirectoryA(izina, sec)`** — umurimo wo hanze muri `kernel32.dll` (external, from `kernel32.dll`)

**`DeleteFileA(izina)`** — umurimo wo hanze muri `kernel32.dll` (external, from `kernel32.dll`)

**`GetEnvironmentVariableA(izina, buf, ingano)`** — umurimo wo hanze muri `kernel32.dll` (external, from `kernel32.dll`)

**`gutegereza(ms)`**  
Gutegereza -- sleep for `ms` milliseconds.

**`igihe()`**  
Igihe -- milliseconds since the system started.

**`nimero_ya_process()`**  
Nimero ya process -- the current process id.

**`amabwiriza()`**  
Amabwiriza yatanzwe -- the raw command line.

**`gukora_ububiko(izina)`**  
Gukora ububiko -- create a directory. Returns non-zero on success.

**`gusiba_dosiye(izina)`**  
Gusiba dosiye -- delete a file. Returns non-zero on success.

**`ikosa_rya_nyuma()`**  
Ikosa rya nyuma -- the last Windows error code.

