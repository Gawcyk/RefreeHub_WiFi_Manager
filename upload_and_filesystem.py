Import("env")  # Import środowiska budowania

def before_upload(source, target, env):
    print("Wgrywanie systemu plików...")
    result = env.Execute("pio run --target uploadfs")
    if result == 0:
        print("System plików wgrany pomyślnie!")
    else:
        print("Błąd wgrywania systemu plików!")

env.AddPreAction("upload", before_upload)
