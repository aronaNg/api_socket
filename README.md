# Projet RC52 API Socket by Corentin DEFIOLLES & Arona NGOM

## api_socket jalon 4

Refactor
-------

- In `client.c`, argument parsing, connection, greeting, menu display, stdin handling, and socket handling each live in their own functions; `main` just wires them together and runs the poll loop. 

- In `server.c`, port parsing, socket setup, client acceptance, role identification, owner/tenant command handling, and polling are split into focused helpers (`parse_port_arg`, `create_listen_socket`, `handle_client_message`, `poll_loop`, etc.), leaving `main` to initialize and launch the loop.

Dans le Jalon 4 nous avons créer une base de données SQL Lite qui stockait dans un premier temps l'historique du serveur
et maintenant des Users et leurs mot de passe (en clair) qui sécurise un minimum l'accès à l'application

Compilation
-----------

- `gcc server.c -o server -lsqlite3`
- `gcc client.c -o client`

Exemple d'exécution dans des terminaux séparés :
-----------
- `./server 8000`
- `./client 127.0.0.1 8000 OWNER Arona aronapass`
- `./client 127.0.0.1 8000 TENANT Corentin corentinpass`

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
gcc server.c -o server -lsqlite3
gcc client.c -o client
```

Authentification
----------------
- Les comptes sont stockés dans `history.db` (table `users`) et pré-remplis :
  - OWNER: `owner / ownerpass`, `Arona / aronapass`
  - TENANT: `tenant / tenantpass`, `Corentin / corentinpass`
- Connexion client : `./client <ip> <port> <ROLE> <pseudo> <password>`
  - Exemple: `./client 127.0.0.1 8080 OWNER owner ownerpass`

Accéder à la base de données:
Commandes:
```bash
sudo apt install sqlite3
sqlite3 history.db
.schema history
SELECT id, datetime(ts,'unixepoch','localtime') AS ts, pseudo, result FROM history ORDER BY ts DESC LIMIT 200;
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
gcc -o server.exe server.c -lsqlite3
gcc -o client.exe client.c
```

