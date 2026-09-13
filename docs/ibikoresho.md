# Ibikoresho — The `wandaa` Project Tool

`wandaac` compiles one file. `wandaa` drives projects: scaffolding, building,
tests, and dependencies.

```
wandaa tangira <izina>          kora umushinga mushya      new project
wandaa ongeraho <izina> <aho>   ongeraho igisabwa          add a dependency
wandaa shakisha                 kuzana ibisabwa            fetch dependencies
wandaa genzura                  genzura ibisabwa           verify against the lockfile
wandaa yubaka                   yubaka umushinga           build
wandaa koresha [args...]        yubaka hanyuma ukoreshe    build and run
wandaa gerageza                 koresha ibigeragezo        run tests
wandaa verisiyo                                            version
```

---

## Gutangira umushinga — Starting a project

```powershell
wandaa tangira urubuga-rwanjye
cd urubuga-rwanjye
wandaa koresha
```

That creates:

```
urubuga-rwanjye/
  wandaa.toml          manifest
  src/mbere.waa        entry point
  tests/mbere_test.waa a test
  .gitignore
```

---

## Manifest — `wandaa.toml`

```toml
[umushinga]
izina = "urubuga-rwanjye"
verisiyo = "0.1.0"
intangiriro = "src/mbere.waa"

[ibisabwa]
ibipimo  = { inzira = "../ibipimo" }
json     = { git = "https://github.com/x/wandaa-json", tag = "v1.0" }
amagambo = "../amagambo"                  # bare string = local path
```

Iyi ni TOML nto kandi ikomeye — this is a deliberately small, strict TOML
subset: sections, `key = value` with quoted strings, and inline tables. An
unknown key is an error naming the line, not something silently ignored.

---

## Ibisabwa — Dependencies

```powershell
wandaa ongeraho ibipimo ../ibipimo
wandaa shakisha
```

`shakisha` vendors each dependency into `ibipapuro/<izina>/` and writes
`wandaa.lock`:

```
# izina	ubwoko	inkomoko	sha256
ibipimo	inzira	../ibipimo	626acef44b076c74777d79fcd04c52445fe9d40a1ea471477159b1dde7aa6928
```

`wandaa yubaka` passes `ibipapuro/` and each package directory to the compiler
as `-I` paths, so a vendored module is imported the normal way:

```wandaa
injiza "ibipimo/ibipimo.waa";
```

### Offline by design

Once `ibipapuro/` is populated — fetched once, or committed to the
repository — **building needs no network at all**. `git` is used only by
`shakisha`, and only for `git` dependencies; a project using local-path
dependencies never needs it.

This is a requirement, not a side effect. A package manager that assumes
reliable broadband is not usable everywhere this language needs to work.

### Genzura — verifying what you have

```powershell
wandaa genzura
```

Re-hashes every vendored package and compares against `wandaa.lock`. A
dependency modified after it was fetched fails the check:

```
  ibipimo: sha256 ntihuye
    wandaa.lock: 626acef44b07...
    ku disiki:   f4583a2ba208...
1 bitahuye.
```

The hash covers every `.waa` file's path and contents, sorted, so it is stable
across machines. `tests/test_cli.sh` checks it against `sha256sum` — an
independent implementation — because a hash nobody verifies is theatre.

---

## Ibigeragezo — Tests

Any `.waa` file in `tests/` is a test. It passes when it exits 0.

```wandaa
reka igisubizo = 2 + 2;
niba (igisubizo != 4) {
  andika("ikosa: 2 + 2 ntabwo ari 4");
  tanga 1;
}
```

```powershell
wandaa gerageza
```

---

## Impinduka z'ibidukikije — Environment variables

| | |
|---|---|
| `WANDAAC` | path to the compiler, if it is not beside `wandaa` |
| `WANDAA_RUNNER` | how to launch a produced `.exe`. Set to `wine` when cross-developing on Linux. |
| `WANDAA_PATH` | an extra module search directory |

---

## Aho module ishakirwa — Module search order

1. Hafi ya dosiye iyinjiza — relative to the importing file
2. `-I` directories (the package tool passes one per vendored package)
3. `WANDAA_PATH`
4. `lib/` beside the compiler, then `../lib` — the standard library, found
   relative to `wandaac` itself so a project anywhere on disk can
   `injiza "imibare.waa"`
5. `lib/` relative to the current directory — for a source checkout
