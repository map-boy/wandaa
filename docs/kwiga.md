# Kwiga Wandaa — Learning Wandaa

*Uburyo bwo kwiga Wandaa uhereye ku busa. A tutorial from zero.*

Nta bumenyi bwa mbere bukenewe usibye kumenya gukoresha terminal.
No prior programming experience assumed beyond using a terminal.

---

## Isomo 1 — Porogaramu ya mbere (Your first program)

Kora dosiye `mbere.waa`:

```wandaa
andika("Mwiriwe Rwanda");
```

Yubake:

```powershell
waa mbere.waa
```

Ibisohoka — output:

```
Mwiriwe Rwanda
```

`mbere.exe` ni porogaramu ya Windows nyayo. Ushobora kuyohereza ku yindi
mudasobwa ya Windows ikore — nta Wandaa ikenewe kuri iyo mudasobwa.

`mbere.exe` is a real Windows program. Copy it to any Windows machine and it
runs — Wandaa does not need to be installed there.

---

## Isomo 2 — Ibigereranyo (Variables)

```wandaa
reka izina = "Keza";
reka imyaka = 20;

andika(izina);
andika(imyaka);
andika(imyaka + 5);
```

`reka` bivuga "reka ... ibe" — *let this be*.

---

## Isomo 3 — Guhitamo (Making decisions)

```wandaa
reka amanota = 75;

niba (amanota >= 80) {
  andika("Byiza cyane");
} ubundi niba (amanota >= 50) {
  andika("Watsinze");
} ubundi {
  andika("Ongera ugerageze");
}
```

Ushobora guhuza ibibazo — you can combine conditions:

```wandaa
niba (amanota >= 50 na amanota < 80) {
  andika("Hagati");
}
```

---

## Isomo 4 — Gusubiramo (Loops)

```wandaa
reka i = 1;
mugihe (i <= 5) {
  andika(i);
  i = i + 1;
}
```

`hagarika` ihagarika loop, `komeza` isimbuka ku igice gikurikira:

```wandaa
reka i = 0;
mugihe (1) {
  i = i + 1;
  niba (i > 100) { hagarika; }       # stop
  niba (i == 13) { komeza; }          # skip this one
  andika(i);
}
```

---

## Isomo 5 — Imirimo (Functions)

```wandaa
umurimo kubara_agaciro(igiciro, umubare) {
  tanga igiciro * umubare;
}

umurimo salamu(izina) {
  tanga "Mwiriwe " + izina;
}

andika(kubara_agaciro(500, 3));
andika(salamu("Mugisha"));
```

Imirimo ishobora kwihamagara — functions can call themselves:

```wandaa
umurimo fibonacci(n) {
  niba (n < 2) { tanga n; }
  tanga fibonacci(n - 1) + fibonacci(n - 2);
}

andika(fibonacci(20));   # 6765
```

---

## Isomo 6 — Intonde (Lists)

```wandaa
reka amanota = [80, 65, 90, 45, 72];

andika(ubunini(amanota));

reka i = 0;
reka igiteranyo = 0;
mugihe (i < ubunini(amanota)) {
  igiteranyo = igiteranyo + amanota[i];
  i = i + 1;
}
andika(igiteranyo / ubunini(amanota));   # impuzandengo (average)
```

Kora urutonde rushya rufite ingano uhisemo:

```wandaa
reka ibyuma = urutonde(10);
ibyuma[0] = 42;
```

---

## Isomo 7 — Amagambo (Text)

```wandaa
reka interuro = "Wandaa ni ururimi rwo mu Rwanda";

andika(uburebure(interuro));
andika(igice(interuro, 0, 6));       # "Wandaa"
andika(inyuguti(interuro, 0));        # 87 — ASCII ya 'W'
andika(mu_ijambo(2026) + " ni umwaka");
```

---

## Isomo 8 — Isomero rusange (The standard library)

```wandaa
injiza "imibare.waa";
injiza "amagambo.waa";
injiza "urutonde.waa";

andika(mugabane(48, 18));                  # 6
andika(hejuru("rwanda"));                  # "RWANDA"
andika(igiteranyo([1, 2, 3, 4, 5]));       # 15
andika(gushyira_ku_murongo([3, 1, 2])[0]); # 1
```

Reba [isomero.md](isomero.md) kugira ngo urebe imirimo yose ihari.

---

## Isomo 9 — Gukoresha Windows (Talking to the system)

Iki ni cyo gituma Wandaa ikora ibintu bikomeye: ishobora guhamagara
umurimo uwo ari wo wose uri muri DLL ya Windows.

This is what makes Wandaa capable: it can call any function in any Windows DLL.

```wandaa
hanze "user32.dll" MessageBoxA(hwnd, ubutumwa, umutwe, ubwoko);
hanze "kernel32.dll" Sleep(ms);

MessageBoxA(0, "Mwiriwe!", "Wandaa", 0);
Sleep(500);
```

Nta glue code, nta wrapper. Ibi ni byo bizatuma Wandaa igera kuri
**database**, **serveri** na **AI** — reba [ROADMAP.md](../ROADMAP.md).

---

## Isomo 10 — Dosiye (Files)

```wandaa
andikamo("amakuru.txt", "Mwiriwe\nRwanda\n");
reka ibirimo = soma("amakuru.txt");
andika(ibirimo);
andika(uburebure(ibirimo));
```

---

## Icyo ukora ubutaha — Where to go next

- [ururimi.md](ururimi.md) — ibisobanuro byuzuye by'ururimi
- [isomero.md](isomero.md) — isomero rusange
- [imbere.md](imbere.md) — uko compiler ikora imbere (compiler internals)
- [../ROADMAP.md](../ROADMAP.md) — ibiri imbere
- [../CONTRIBUTING.md](../CONTRIBUTING.md) — uko wafasha
