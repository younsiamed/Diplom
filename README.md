Клонируйте vcpkg: `git clone https://github.com/microsoft/vcpkg.git`

Запустите `bootstrap-vcpkg.bat`

Установите пакеты: `./vcpkg install boost-beast:x64-windows boost-system:x64-windows boost-locale:x64-windows libpqxx:x64-windows pugixml:x64-windows openssl:x64-windows`

Выполните `./vcpkg integrate install`

Установите PostgreSQL, пароль `12345`

Добавьте путь к bin `Панель управления > Система > Дополнительные параметры системы > Переменные среды > Path > Edit > New` - `C:\Program Files\PostgreSQL\18\bin`

Выполните в CMD `psql -U postgres -h localhost -p 5432 -c "CREATE DATABASE search_db`

Соберите проект

Выполните и подождите `./spider.exe`

Выполните `./searcher.exe.`

Откройте в браузере http://localhost:8080

Введите запрос
