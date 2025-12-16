# System requirements et commandes d'installation

Ce projet est en C et utilise SQLite. Voici les paquets système nécessaires et les commandes d'installation pour Linux (Debian/WSL) et Windows (MSYS2 / Chocolatey).

## Debian / Ubuntu / WSL
Paquets requis:
- build-essential (gcc, make, etc.)
- libsqlite3-dev (headers de SQLite)
- pkg-config (optionnel)

Commandes:
```bash
sudo apt update
sudo apt install -y build-essential libsqlite3-dev pkg-config
```

Compiler:
```bash
gcc -Wall -Wextra -O2 -o server server.c -lsqlite3
gcc -Wall -Wextra -O2 -o corclient Corclient.c
```

Accéder à la base de données:
Commandes:
```bash
sudo apt install sqlite3
sqlite3 history.db
.schema history
SELECT id, datetime(ts,'unixepoch','localtime') AS ts, pseudo, event FROM history ORDER BY ts DESC LIMIT 200;
```

## MSYS2 / MinGW (recommandé sur Windows pour gcc)
Installer MSYS2 depuis https://www.msys2.org puis ouvrir `MINGW64` shell.
Paquets requis:
- mingw-w64-x86_64-gcc
- mingw-w64-x86_64-make (optionnel)
- mingw-w64-x86_64-sqlite3

Commandes MSYS2 (MINGW64):
```bash
pacman -Syu
pacman -S --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-sqlite3
```

Compiler (dans MINGW64 shell):
```bash
gcc -Wall -Wextra -O2 -o server.exe server.c -lsqlite3
gcc -Wall -Wextra -O2 -o corclient.exe Corclient.c
```

## Windows (Chocolatey) — alternative
Chocolatey peut installer des outils, mais la chaîne MSYS2/Mingw est preferable pour compiler avec `gcc`.
Exemple (PowerShell admin):
```powershell
choco install -y msys2
# puis lancez msys2 et utilisez pacman comme ci-dessus
```
