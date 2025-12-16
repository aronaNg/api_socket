# api_socket jalon 3

Refactor
-------

- In `Corclient.c`, argument parsing, connection, greeting, menu display, stdin handling, and socket handling each live in their own functions; `main` just wires them together and runs the poll loop. 
- In `server.c`, port parsing, socket setup, client acceptance, role identification, owner/tenant command handling, and polling are split into focused helpers (`parse_port_arg`, `create_listen_socket`, `handle_client_message`, `poll_loop`, etc.), leaving `main` to initialize and launch the loop.


Compilation
-----------

- `gcc server.c -o server -lsqlite3`
- `gcc Corclient.c -o Corclient`