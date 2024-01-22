#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>

using namespace std::literals;

//#define DEBUG
#ifdef DEBUG
#define LOG(LOC) LOC
#else
#define LOG(LOC)
#endif

/*
 * Funkeys! (Linux version)
 * Same intent and name but a completely different code base and mechanism than the Windows version.
 * Inspired by xcape and caps2esc, but made to be leaner than either of those tools and solve
 * just one particular problem I care about.
 * You *can* make it do something else, but you'll need to edit the code and rebuild it.
 *
 * So what does it do?
 * Of course, it remaps CapsLock to LCtrl and LCtrl to CapsLock, it also remaps RCtrl to Enter,
 * and Enter is remapped to RCtrl but only if you hold it long enough or use with other keys,
 * otherwise it just works like Enter. Duh!
 * That's why it's called funkeys :)
 *
 * Idk why you would want to use it elsewhere but this is the license:
 * https://glm.g-truc.net/copying.txt (The Happy Bunny License (Modified MIT License))
 *
 * To build:
 * $ c++ -I/usr/include/libevdev-1.0 -g -std=c++17 -o funkeys funkeys.cpp -levdev
 */

/*
 * Documentation & reference materials:
 * /usr/share/doc/libevdev-dev/html/index.html
 * /usr/share/doc/libevdev-dev/html/group__uinput.html
 * /usr/share/doc/linux-doc/input/event-codes.rst.gz
 * https://gitlab.freedesktop.org/libevdev/libevdev/blob/master/tools/libevdev-events.c
 *
 * Input code list: /usr/include/linux/input-event-codes.h
 */

std::string type_to_string(unsigned type) {
    std::string s;
    switch (type) {
        case EV_SYN: s = "EV_SYN"; break;
        case EV_KEY: s = "EV_KEY"; break;
        case EV_REL: s = "EV_REL"; break;
        case EV_ABS: s = "EV_ABS"; break;
        case EV_MSC: s = "EV_MSC"; break;
        case EV_LED: s = "EV_LED"; break;
        default:
            s = std::to_string(type);
    }
    return s;
}

std::string key_to_string(unsigned type) {
    std::string s;
    switch (type) {
        
        default:
            s = std::to_string(type);
    }
    return s;
}

std::string event_to_string(const input_event& ev) {
    std::stringstream ss;

    std::string type = std::to_string(ev.type);
    std::string code = std::to_string(ev.code);
    std::string value = std::to_string(ev.value);

    switch (ev.type) {
    case EV_SYN: type = "EV_SYN"; break;
    case EV_KEY:
        type = "EV_KEY";
        switch (ev.code) {
            case KEY_ENTER: code = "KEY_ENTER"; break;
            case KEY_RIGHTCTRL: code = "KEY_RIGHTCTRL"; break;
            case KEY_LEFTCTRL: code = "KEY_LEFTCTRL"; break;
            case KEY_CAPSLOCK: code = "KEY_CAPSLOCK"; break;
        }
        break;
    case EV_REL: type = "EV_REL"; break;
    case EV_ABS: type = "EV_ABS"; break;
    case EV_MSC: type = "EV_MSC"; break;
    case EV_LED: type = "EV_LED"; break;
    }

    ss << "type = " << type << ", code = " << code << ", value = " << value;

    return ss.str();
}

template<typename T>
void send_event(libevdev_uinput *dev, T&& ev) {
    int err;
    LOG(std::cout << " -> " << event_to_string(ev));
    if ((err = libevdev_uinput_write_event(dev, ev.type, ev.code, ev.value)))
        throw std::runtime_error(strerror(err));
}

void send_syn(libevdev_uinput *dev) {
    input_event ev {{}, EV_SYN, SYN_REPORT, 0};
    send_event(dev, ev);
}

void handle_event(libevdev_uinput *dev_clone, struct input_event& ev) {
    std::string type = type_to_string(ev.type);
    static bool enter_pending = false;
    static std::chrono::time_point<std::chrono::system_clock> last_enter_press;

    LOG(std::cout << "\nReceived event: " << event_to_string(ev) << " Converted to: ");

    auto delay = 200ms;
    // if (ev.type == EV_MSC)
    //     return;
    if (ev.type == EV_KEY) {
        if (ev.code == KEY_ENTER && ev.value == 0 && enter_pending && std::chrono::system_clock::now() - last_enter_press < delay) {
            LOG(std::cout << "<Enter up>");
            enter_pending = false;
            // Simulate the release of the ctrl key we "held" preemptively
            send_event(dev_clone, input_event{{}, EV_KEY, KEY_RIGHTCTRL, 0});
            send_syn(dev_clone);
            // Simulate press and release for "enter"
            send_event(dev_clone, input_event{{}, EV_MSC, MSC_SCAN, 458792});
            send_event(dev_clone, input_event{{}, EV_KEY, KEY_ENTER, 1});
            send_syn(dev_clone);
            send_event(dev_clone, input_event{{}, EV_MSC, MSC_SCAN, 458792});
            ev.code = KEY_ENTER;
        } else {
            enter_pending = false;
            switch (ev.code) {
            case KEY_CAPSLOCK:
                ev.code = KEY_LEFTCTRL;
                break;
            case KEY_LEFTCTRL:
                ev.code = KEY_CAPSLOCK;
                break;
            case KEY_RIGHTCTRL:
                ev.code = KEY_ENTER;
                break;
            case KEY_ENTER:
                enter_pending = true;
                ev.code = KEY_RIGHTCTRL;
                if (ev.value == 1) {
                    // If this is a new keypress (and not repeat), remember when we pressed the key.
                    last_enter_press = std::chrono::system_clock::now();
                }
                break;
            default:
                break;
            }
        }
    }
    send_event(dev_clone, ev);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Oopsie... need 1 argument!\n"
                     "Like dis:\n"
                     "$ sudo funkeys /dev/input/by-id/blahblahblah\n\n"
                     "Heres a list of all devices:\n";
        for (const auto & entry : std::filesystem::directory_iterator("/dev/input/by-id/"))
            std::cout << entry.path().string() << std::endl;
        std::cerr << "\nhehe :)\n"
                     "... funkeys!\n\n"
                     "FATAL ERROR. TERMINATING THE ROGUE PROCESS.\n";
        // 🤔
        return 1;
    }

    libevdev *dev_from;
    int fd;

    // Sleep for a bit, otherwise the "enter" key gets stuck, not sure why.
    std::this_thread::sleep_for(200ms);

    // Open the original device
    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        std::cerr << "Can't open device " << argv[1] << ": " << strerror(errno) << '\n';
        return 1;
    }
    if (libevdev_new_from_fd(fd, &dev_from) < 0) {
        std::cerr << "Can't create evdev from fd.\n";
        return 1;
    }
    std::cout << "Attached to " << libevdev_get_name(dev_from) << '\n';

    libevdev_uinput *dev_clone;
    if (libevdev_uinput_create_from_device(dev_from, LIBEVDEV_UINPUT_OPEN_MANAGED, &dev_clone)) {
        std::cerr << "Can't duplicate the input device.\n";
        return 1;
    }

    if (libevdev_grab(dev_from, LIBEVDEV_GRAB) < 0) {
        std::cerr << "Can't grab that input device.\n";
        return 1;
    }

    constexpr unsigned flags = LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING;
    while (true) {
        struct input_event ev;
        int i = 0, err;
        switch (err = libevdev_next_event(dev_from, flags, &ev)) {
            case LIBEVDEV_READ_STATUS_SYNC:
                while (libevdev_next_event(dev_from, LIBEVDEV_READ_FLAG_SYNC, &ev) == LIBEVDEV_READ_STATUS_SYNC) {
                    // Skip the dropped event.
                    // (Not sure if there's a better way to handle it, but it seems to be the safest way.)
                    i++;
                }
                std::cout << "Oops, dropped " << i << " events.\n";
                break;
            case LIBEVDEV_READ_STATUS_SUCCESS:
                handle_event(dev_clone, ev);
                break;
            case -EAGAIN:
                break;
            default:
                throw std::runtime_error(strerror(err));
        }
    }
}