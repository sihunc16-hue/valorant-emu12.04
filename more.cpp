// vgc_emulator_1to1.cpp - MANUAL VERSION SELECTION
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <TlHelp32.h>
#include <vector>
#include <thread>
#include <atomic>
#include <iostream>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <ctime>
#include <regex>
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")

#define CURRENT_VERSION 5

std::atomic_bool shutdown_event(false);
std::atomic_bool stopped_once(false);
std::atomic<HANDLE> g_current_pipe(nullptr);

const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\933823D3-C77B-4BAE-89D7-A92B567236BC";

struct VanguardHeader {
    uint32_t magic;
    uint32_t total_size;
    uint32_t message_type;
    uint8_t  unknown1[12];
    uint32_t payload_size;
    uint8_t  unknown2[8];
};

void log_message(const char* msg) {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::tm bt;
    localtime_s(&bt, &now_time_t);

    std::cout << "["
        << std::put_time(&bt, "%H:%M:%S")
        << "." << std::setfill('0') << std::setw(3) << now_ms.count()
        << "] [VGC] " << msg << std::endl;
}

void log_hex(const uint8_t* data, size_t size, const char* prefix = "") {
    std::stringstream ss;
    ss << prefix << " Hex: ";
    for (size_t i = 0; i < min(size, 32); i++) {
        ss << std::hex << std::setfill('0') << std::setw(2) << (int)data[i] << " ";
        if ((i + 1) % 16 == 0 && i < size - 1) {
            log_message(ss.str().c_str());
            ss.str("");
            ss << "       ";
        }
    }
    if (ss.str().length() > 0) {
        log_message(ss.str().c_str());
    }
}

void uuid_string_to_binary(const char* uuid_str, uint8_t* binary) {
    unsigned int parts[16];
    sscanf_s(uuid_str, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        &parts[0], &parts[1], &parts[2], &parts[3],
        &parts[4], &parts[5],
        &parts[6], &parts[7],
        &parts[8], &parts[9],
        &parts[10], &parts[11], &parts[12], &parts[13], &parts[14], &parts[15]);

    binary[0] = parts[3] & 0xFF;
    binary[1] = parts[2] & 0xFF;
    binary[2] = parts[1] & 0xFF;
    binary[3] = parts[0] & 0xFF;
    binary[4] = parts[5] & 0xFF;
    binary[5] = parts[4] & 0xFF;
    binary[6] = parts[7] & 0xFF;
    binary[7] = parts[6] & 0xFF;
    binary[8] = parts[8] & 0xFF;
    binary[9] = parts[9] & 0xFF;
    binary[10] = parts[10] & 0xFF;
    binary[11] = parts[11] & 0xFF;
    binary[12] = parts[12] & 0xFF;
    binary[13] = parts[13] & 0xFF;
    binary[14] = parts[14] & 0xFF;
    binary[15] = parts[15] & 0xFF;
}

bool find_last_uuid(const uint8_t* data, size_t size, uint8_t* uuid_bin, char* uuid_str) {
    std::string text;
    for (size_t i = 0; i < size; i++) {
        text += (data[i] >= 32 && data[i] <= 126) ? (char)data[i] : ' ';
    }

    std::regex uuid_pattern("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}");
    std::smatch match;
    std::string last_uuid;

    auto search_start = text.cbegin();
    while (std::regex_search(search_start, text.cend(), match, uuid_pattern)) {
        last_uuid = match[0];
        search_start = match[0].second;
    }

    if (!last_uuid.empty()) {
        strcpy_s(uuid_str, 37, last_uuid.c_str());
        uuid_string_to_binary(last_uuid.c_str(), uuid_bin);
        return true;
    }
    return false;
}

void stop_and_restart_vgc() {
    system("sc stop vgc >nul 2>&1");
    Sleep(500);
    system("sc start vgc >nul 2>&1");
    Sleep(500);
}

void override_vgc_pipe() {
    HANDLE pipe = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe);
        log_message("Pipe overridden");
    }
}

std::vector<uint8_t> create_server_ack(uint32_t magic) {
    std::vector<uint8_t> resp;
    VanguardHeader hdr = { 0 };

    hdr.magic = magic + 1;
    hdr.total_size = 40;
    hdr.message_type = 1;
    hdr.payload_size = 8;

    resp.insert(resp.end(), (uint8_t*)&hdr, (uint8_t*)&hdr + sizeof(hdr));
    resp.insert(resp.end(), 8, 0);

    return resp;
}

std::vector<uint8_t> create_auth_ack(uint32_t magic, int version, const uint8_t* uuid_bin) {
    std::vector<uint8_t> resp;
    VanguardHeader hdr = { 0 };

    char msg[256];
    sprintf_s(msg, "Using version %d", version);
    log_message(msg);

    switch (version) {
    case 1:
        hdr.magic = magic + 1;
        hdr.total_size = 40;
        hdr.message_type = 1;
        hdr.payload_size = 8;
        resp.insert(resp.end(), (uint8_t*)&hdr, (uint8_t*)&hdr + sizeof(hdr));
        resp.insert(resp.end(), 8, 0);
        log_hex(resp.data(), resp.size(), "Version 1:");
        break;

    case 2:
        hdr.magic = magic + 1;
        hdr.total_size = 40;
        hdr.message_type = 1;
        hdr.payload_size = 8;
        resp.insert(resp.end(), (uint8_t*)&hdr, (uint8_t*)&hdr + sizeof(hdr));
        resp.insert(resp.end(), uuid_bin, uuid_bin + 8);
        log_hex(resp.data(), resp.size(), "Version 2:");
        break;

    case 3:
        hdr.magic = magic + 1;
        hdr.total_size = 40 + 16;
        hdr.message_type = 1;
        hdr.payload_size = 16;
        resp.insert(resp.end(), (uint8_t*)&hdr, (uint8_t*)&hdr + sizeof(hdr));
        resp.insert(resp.end(), uuid_bin, uuid_bin + 16);
        log_hex(resp.data(), resp.size(), "Version 3:");
        break;

    case 4:
    {
        hdr.magic = magic + 1;
        hdr.total_size = 40;
        hdr.message_type = 1;
        hdr.payload_size = 8;
        resp.insert(resp.end(), (uint8_t*)&hdr, (uint8_t*)&hdr + sizeof(hdr));
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        resp.insert(resp.end(), (uint8_t*)&timestamp, (uint8_t*)&timestamp + 8);
        log_hex(resp.data(), resp.size(), "Version 4:");
    }
    break;

    case 5:
    {
        hdr.magic = magic + 1;
        hdr.total_size = 40 + 16 + 8;
        hdr.message_type = 1;
        hdr.payload_size = 16 + 8;
        resp.insert(resp.end(), (uint8_t*)&hdr, (uint8_t*)&hdr + sizeof(hdr));
        resp.insert(resp.end(), uuid_bin, uuid_bin + 16);
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        resp.insert(resp.end(), (uint8_t*)&timestamp, (uint8_t*)&timestamp + 8);
        log_hex(resp.data(), resp.size(), "Version 5:");
    }
    break;
    }

    return resp;
}

std::vector<uint8_t> create_heartbeat_response(const uint8_t* data, size_t size) {
    std::vector<uint8_t> resp(data, data + size);
    VanguardHeader* hdr = (VanguardHeader*)resp.data();
    hdr->magic += 1;
    return resp;
}

void handle_client(HANDLE pipe) {
    std::vector<uint8_t> buffer(16384);
    DWORD bytesRead;
    g_current_pipe.store(pipe);

    uint8_t uuid_bin[16] = { 0 };
    char uuid_str[37] = { 0 };
    bool uuid_found = false;

    log_message("=== NEW CONNECTION ===");
    char version_msg[64];
    sprintf_s(version_msg, "Using FIXED version #%d", CURRENT_VERSION);
    log_message(version_msg);

    while (!shutdown_event.load()) {
        if (!ReadFile(pipe, buffer.data(), (DWORD)buffer.size(), &bytesRead, NULL) || bytesRead == 0)
            break;

        VanguardHeader* hdr = (VanguardHeader*)buffer.data();

        char msg[256];
        sprintf_s(msg, "RECV: magic=0x%02X, type=%u, size=%u",
            hdr->magic, hdr->message_type, bytesRead);
        log_message(msg);

        std::vector<uint8_t> response;

        switch (hdr->message_type) {
        case 2:
            log_message("Server list request");
            response = create_server_ack(hdr->magic);
            if (!stopped_once.exchange(true)) {
                system("sc stop vgc >nul 2>&1");
                Beep(1000, 500);
            }
            break;

        case 4:
            log_message("Auth token - SEARCHING FOR UUID");
            uuid_found = find_last_uuid(buffer.data(), bytesRead, uuid_bin, uuid_str);

            if (uuid_found) {
                char uuid_msg[128];
                sprintf_s(uuid_msg, "Found UUID: %s", uuid_str);
                log_message(uuid_msg);
                log_hex(uuid_bin, 16, "UUID binary:");

                response = create_auth_ack(hdr->magic, CURRENT_VERSION, uuid_bin);
            }
            else {
                log_message("UUID NOT found - using v1");
                response = create_auth_ack(hdr->magic, 1, uuid_bin);
            }
            break;

        case 1:
            log_message("Heartbeat received");
            response = create_heartbeat_response(buffer.data(), bytesRead);
            break;

        default:
            response = create_heartbeat_response(buffer.data(), bytesRead);
            break;
        }

        if (!response.empty()) {
            DWORD written;
            WriteFile(pipe, response.data(), (DWORD)response.size(), &written, NULL);
            log_message("Response sent");
        }

        Sleep(10);
    }

    CloseHandle(pipe);
    g_current_pipe.store(nullptr);

    log_message("=== CONNECTION CLOSED ===");
}

void create_named_pipe() {
    while (!shutdown_event.load()) {
        HANDLE pipe = CreateNamedPipeW(PIPE_NAME, PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1, 1048576, 1048576, 500, NULL);

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }

        log_message("Waiting for client...");

        if (ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
            log_message("Client connected!");
            std::thread(handle_client, pipe).detach();
        }
        else {
            CloseHandle(pipe);
        }
    }
}

bool is_valorant_running() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = { sizeof(pe) };

    if (Process32FirstW(snap, &pe)) do {
        if (_wcsicmp(pe.szExeFile, L"VALORANT-Win64-Shipping.exe") == 0) {
            CloseHandle(snap);
            return true;
        }
    } while (Process32NextW(snap, &pe));

    CloseHandle(snap);
    return false;
}

BOOL WINAPI ConsoleHandler(DWORD dwType) {
    if (dwType == CTRL_C_EVENT || dwType == CTRL_CLOSE_EVENT) {
        shutdown_event.store(true);
        return TRUE;
    }
    return FALSE;
}

int main() {
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    log_message("=== VGC Emulator - MANUAL VERSION SELECTION ===");
    log_message("Version 1: 8 zero bytes");
    log_message("Version 2: first 8 bytes of UUID");
    log_message("Version 3: full 16 bytes of UUID");
    log_message("Version 4: timestamp");
    log_message("Version 5: UUID + timestamp");
    log_message("");

    char current[64];
    sprintf_s(current, "ACTIVE VERSION: %d", CURRENT_VERSION);
    log_message(current);
    log_message("");

    stop_and_restart_vgc();
    override_vgc_pipe();

    std::thread(create_named_pipe).detach();

    log_message("Waiting for Valorant...");
    while (!is_valorant_running() && !shutdown_event.load()) Sleep(500);
    log_message("Valorant detected!");

    while (!shutdown_event.load()) Sleep(1000);

    log_message("Shutting down...");
    return 0;
}