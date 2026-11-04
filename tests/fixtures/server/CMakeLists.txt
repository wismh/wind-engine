# wind_server_fixture: the server process tests/server_process_test.cpp starts. Made by engine_add_server, in its own
# directory because the function reads include/, src/, and assets/ from the calling directory.
engine_add_server(wind_server_fixture fixture_server.cpp)
