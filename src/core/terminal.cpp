#include "terminal.h"
#include "sensor_data.h"
#include "../config.h"

#include <SD.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <map>

// Per-client working directory
static std::map<uint32_t, String> s_cwd;

static String client_cwd(uint32_t id) {
    auto it = s_cwd.find(id);
    return (it != s_cwd.end()) ? it->second : "/";
}

// Resolve a path relative to CWD
static String resolve(uint32_t id, const String& arg) {
    if (arg.length() == 0) return client_cwd(id);
    if (arg.startsWith("/")) return arg;
    String base = client_cwd(id);
    if (!base.endsWith("/")) base += "/";
    return base + arg;
}

// Build response JSON
static String make_resp(int seq, const String& cwd, const String& output, bool ok) {
    JsonDocument doc;
    doc["type"]   = "terminal_out";
    doc["seq"]    = seq;
    doc["output"] = output;
    doc["ok"]     = ok;
    doc["cwd"]    = cwd;
    String out;
    serializeJson(doc, out);
    return out;
}

// ---- Command implementations ----

static String cmd_ls(uint32_t id, const String& arg) {
    String path = resolve(id, arg);
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) return "Not a directory: " + path;
    String result;
    File entry = dir.openNextFile();
    while (entry) {
        result += (entry.isDirectory() ? "[DIR] " : "      ");
        result += entry.name();
        if (!entry.isDirectory()) {
            result += "  (" + String(entry.size()) + " B)";
        }
        result += "\n";
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
    if (result.isEmpty()) result = "(empty)";
    return result;
}

static String cmd_cat(uint32_t id, const String& arg) {
    if (arg.isEmpty()) return "Usage: cat <file>";
    String path = resolve(id, arg);
    if (!SD.exists(path)) return "File not found: " + path;
    File f = SD.open(path, FILE_READ);
    if (!f) return "Cannot open: " + path;
    if (f.isDirectory()) { f.close(); return "Is a directory: " + path; }
    if (f.size() > 8192) { f.close(); return "File too large to display (> 8KB)"; }
    String content;
    while (f.available()) content += (char)f.read();
    f.close();
    return content;
}

static String cmd_cd(uint32_t id, const String& arg) {
    String path = resolve(id, arg);
    // Normalize trailing slash
    while (path.length() > 1 && path.endsWith("/")) path.remove(path.length() - 1);
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) return "Not a directory: " + path;
    dir.close();
    s_cwd[id] = path;
    return "";
}

static String cmd_rm(uint32_t id, const String& arg) {
    if (arg.isEmpty()) return "Usage: rm <file>";
    String path = resolve(id, arg);
    if (!SD.exists(path)) return "Not found: " + path;
    if (SD.remove(path)) return "Deleted: " + path;
    return "Delete failed: " + path;
}

static String cmd_mkdir(uint32_t id, const String& arg) {
    if (arg.isEmpty()) return "Usage: mkdir <path>";
    String path = resolve(id, arg);
    if (SD.mkdir(path)) return "Created: " + path;
    return "mkdir failed: " + path;
}

static String cmd_i2c_scan() {
    Wire.begin();
    String result = "I2C scan:\n";
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "  0x%02X (%d)\n", addr, addr);
            result += buf;
            found++;
        }
    }
    result += found ? (String(found) + " device(s) found") : "No devices found";
    return result;
}

static String cmd_gpio_read(const String& arg) {
    if (arg.isEmpty()) return "Usage: gpio read <pin>";
    int pin = arg.toInt();
    pinMode(pin, INPUT);
    return "GPIO " + String(pin) + " = " + String(digitalRead(pin));
}

static String cmd_gpio_write(const String& args) {
    int sp = args.indexOf(' ');
    if (sp < 0) return "Usage: gpio write <pin> <0|1>";
    int pin = args.substring(0, sp).toInt();
    int val = args.substring(sp + 1).toInt();
    pinMode(pin, OUTPUT);
    digitalWrite(pin, val ? HIGH : LOW);
    return "GPIO " + String(pin) + " set to " + String(val ? 1 : 0);
}

static String cmd_lora_tx(const String& msg) {
    if (msg.isEmpty()) return "Usage: lora tx <message>";
    SensorLock lock;
    g_sensors.lora_tx_pending = true;
    g_sensors.lora_tx_payload = msg;
    return "LoRa TX queued: " + msg;
}

static String cmd_help() {
    return
        "File system:\n"
        "  ls [path]           list directory\n"
        "  cat <file>          print file content\n"
        "  cd <path>           change directory\n"
        "  rm <file>           delete file\n"
        "  mkdir <path>        create directory\n"
        "  pwd                 current directory\n"
        "\nSystem:\n"
        "  heap                free heap\n"
        "  uptime              device uptime\n"
        "  version             firmware version\n"
        "  reboot              restart device\n"
        "\nHardware:\n"
        "  i2c scan            scan I2C bus\n"
        "  gpio read <pin>     read GPIO pin\n"
        "  gpio write <p> <v>  write GPIO pin\n"
        "  lora tx <msg>       transmit LoRa message\n";
}

// ---- Dispatcher ----

String terminal_exec(uint32_t client_id, const String& cmd_line, int seq) {
    String trimmed = cmd_line;
    trimmed.trim();
    if (trimmed.isEmpty()) {
        return make_resp(seq, client_cwd(client_id), "", true);
    }

    // Split command and arguments
    int sp = trimmed.indexOf(' ');
    String cmd = (sp < 0) ? trimmed : trimmed.substring(0, sp);
    String arg = (sp < 0) ? "" : trimmed.substring(sp + 1);
    arg.trim();
    cmd.toLowerCase();

    String output;
    bool ok = true;

    if (cmd == "help") {
        output = cmd_help();
    } else if (cmd == "pwd") {
        output = client_cwd(client_id);
    } else if (cmd == "ls") {
        output = cmd_ls(client_id, arg);
    } else if (cmd == "cat") {
        output = cmd_cat(client_id, arg);
    } else if (cmd == "cd") {
        output = cmd_cd(client_id, arg);
    } else if (cmd == "rm") {
        output = cmd_rm(client_id, arg);
    } else if (cmd == "mkdir") {
        output = cmd_mkdir(client_id, arg);
    } else if (cmd == "heap") {
        output = "Free heap: " + String(esp_get_free_heap_size() / 1024) + " KB";
    } else if (cmd == "uptime") {
        uint32_t s;
        { SensorLock lock; s = g_sensors.sys_uptime; }
        char buf[32];
        snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", s/3600, (s%3600)/60, s%60);
        output = "Uptime: " + String(buf);
    } else if (cmd == "version") {
        output = "dashIO " DASHIO_VERSION;
    } else if (cmd == "reboot") {
        output = "Rebooting...";
        make_resp(seq, client_cwd(client_id), output, true);
        delay(500);
        ESP.restart();
    } else if (cmd == "i2c") {
        if (arg == "scan") output = cmd_i2c_scan();
        else { output = "Usage: i2c scan"; ok = false; }
    } else if (cmd == "gpio") {
        int sp2 = arg.indexOf(' ');
        String sub = (sp2 < 0) ? arg : arg.substring(0, sp2);
        String rest = (sp2 < 0) ? "" : arg.substring(sp2 + 1);
        if (sub == "read")       output = cmd_gpio_read(rest);
        else if (sub == "write") output = cmd_gpio_write(rest);
        else { output = "Usage: gpio read|write ..."; ok = false; }
    } else if (cmd == "lora") {
        int sp2 = arg.indexOf(' ');
        String sub = (sp2 < 0) ? arg : arg.substring(0, sp2);
        String rest = (sp2 < 0) ? "" : arg.substring(sp2 + 1);
        if (sub == "tx") output = cmd_lora_tx(rest);
        else { output = "Usage: lora tx <message>"; ok = false; }
    } else {
        output = "Unknown command: " + cmd + ". Type 'help' for list.";
        ok = false;
    }

    return make_resp(seq, client_cwd(client_id), output, ok);
}

void terminal_client_disconnected(uint32_t client_id) {
    s_cwd.erase(client_id);
}
