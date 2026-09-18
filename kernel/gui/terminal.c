#include "app_internal.h"
#include "../hw.h"
#include "../net/http.h"
#include "../pmm.h"

static char command[80], output[1024] = "Type HELP to see available commands.";
static int cmdlen;
static void wrapped(int x, int y, int bottom, const char *s, int cols, u32 color) {
    int col = 0;
    while (*s && y + 20 <= bottom) {
        char tmp[2] = {*s++, 0};
        if (tmp[0] == '\n') { y += 20; col = 0; continue; }
        mono(x + col * 12, y, tmp, color);
        if (++col == cols) { col = 0; y += 20; }
    }
}
void terminal_render(int width, int height, int active) {
    (void)active;
    int prompt_y = height - 90, cols = (width - 68) / 12;
    int command_cols = (width - 68) / 12;
    if (cols < 1) cols = 1;
    if (command_cols < 1) command_cols = 1;
    int start = cmdlen >= command_cols ? cmdlen - command_cols + 1 : 0;
    text(25, 64, OS_NAME " Terminal", 0xc5c8df, 2);
    wrapped(22, 107, prompt_y - 8, output, cols, 0xd9dce9);
    mono(22, prompt_y, ">", 0xc8b6ef);
    mono(46, prompt_y, command + start, 0xf4f3fa);
    rect(46 + (cmdlen - start) * 12, prompt_y + 16, 9, 1, 0xc8b6ef);
    app_label(25, height - 32, width - 50, "help  /  save  /  ps  /  net  /  ping  /  reboot", 0x8c91ac, 1);
}
static int valid_name(const char *s) {
    int n = len(s);
    if (n < 1 || n > 23) return 0;
    for (int i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '.' ||
              s[i] == '_' || s[i] == '-')) return 0;
    return 1;
}
static void execute(void) {
    char *arg = command;
    while (*arg && *arg != ' ') arg++;
    if (*arg) *arg++ = 0;
    if (eq(command, "help"))
        copy(output, "LS / NEW name / OPEN name / RM name / SAVE\nCAT / PS / PAUSE pid / RESUME "
                     "pid\nKILL pid / SPAWN pid / FAULTTEST\nNET / PING / PUBLICIP / ANIM / THEME / REBOOT\n"
                     "Notes: CTRL+S saves. PGUP/PGDN scroll.");
    else if (eq(command, "about"))
        copy(output,
             OS_LABEL "\nOwn kernel, PollikFS, RTL8139 + ARP/ICMP.\nTwo ring3 workers "
                      "with private 64 KiB\nsegments, timer preemption and syscalls.\nDesktop apps "
                      "still run in the kernel.");
    else if (eq(command, "gfx")) framebuffer_info(output);
    else if (eq(command, "mem"))
        copy(output,
             "Scene + wallpaper cache: 6144 KiB\nUser segments: 2 x 64 KiB\nEach worker has a "
             "private kernel stack.\nFixed physical layout, no paging/heap.");
    else if (eq(command, "save"))
        copy(output, notes_save() ? "Saved to PollikFS on data disk." : "Save failed. Check data disk.");
    else if (eq(command, "new")) {
        int slot = -1;
        for (int i = 0; i < FS_FILES; i++)
            if (!files[i].name[0]) { slot = i; break; }
        if (!valid_name(arg)) copy(output, "Use a filename of 1-23 letters/digits/._-");
        else if (fs_find(arg) >= 0) copy(output, "File already exists.");
        else if (slot < 0) copy(output, "Disk directory full (8 files maximum).");
        else if (notes_changed() && !notes_save()) copy(output, "Current note could not be saved.");
        else if (fs_save(slot, arg, "", 0)) {
            notes_open(slot);
            copy(output, "Created file on disk.");
        } else copy(output, "Could not create file.");
    } else if (eq(command, "open")) {
        int i = fs_find(arg);
        if (i < 0) copy(output, "File not found.");
        else { notes_open(i); copy(output, "Opened document."); }
    } else if (eq(command, "rm")) {
        int i = fs_find(arg);
        if (i < 0) copy(output, "File not found.");
        else if (i == notes_selected_file()) copy(output, "Open another document before removing this one.");
        else copy(output, fs_remove(i) ? "File removed from disk." : "Remove failed.");
    } else if (eq(command, "ls")) {
        int pos = 0;
        output[0] = 0;
        for (int i = 0; i < FS_FILES; i++)
            if (files[i].name[0]) {
                int j = 0;
                while (files[i].name[j] && pos < 237) output[pos++] = files[i].name[j++];
                output[pos++] = '\n';
            }
        output[pos] = 0;
    } else if (eq(command, "ps")) process_list(output);
    else if (eq(command, "pause") || eq(command, "resume") || eq(command, "kill") || eq(command, "spawn")) {
        int action = eq(command, "pause") ? 0 : eq(command, "resume") ? 1 : eq(command, "kill") ? 2 : 3;
        if (len(arg) != 1 || !process_action(*arg - '0', action)) copy(output, "Use PID 1 or 2.");
        else process_list(output);
    } else if (eq(command, "faulttest")) {
        process_fault_test();
        copy(output, "Worker 1 will attempt a privileged instruction.\nOnly that process should "
                     "stop. Inspect PS.");
    } else if (eq(command, "net")) net_info(output);
    else if (eq(command, "ping")) {
        net_ping();
        copy(output, "Testing gateway 10.0.2.2...\nRun NET for the result, or open Settings.");
    } else if (eq(command, "publicip")) {
        char ip[48];
        if (http_get_public_ip(ip, sizeof(ip))) {
            copy(output, "Public IP: ");
            int at = len(output), i = 0;
            while (ip[i] && at < 239) output[at++] = ip[i++];
            output[at] = 0;
        } else copy(output, "Could not fetch public IP over verified HTTPS.");
    } else if (eq(command, "cat")) {
        int i = 0;
        const char *note = notes_text();
        while (note[i] && i < 239) { output[i] = note[i]; i++; }
        output[i] = 0;
    } else if (eq(command, "theme")) {
        app_host_set_theme(!app_host_theme());
        copy(output, "Wallpaper changed.");
    } else if (eq(command, "anim") || eq(command, "animations")) {
        if (eq(arg, "off") || eq(arg, "0")) {
            app_host_set_animations(0);
            app_host_stop_minimize();
            copy(output, "Animations disabled (Fast mode).");
        } else if (eq(arg, "on") || eq(arg, "1")) {
            app_host_set_animations(1);
            copy(output, "Animations enabled (Smooth mode).");
        } else {
            app_host_set_animations(!app_host_animations());
            copy(output, app_host_animations() ? "Animations enabled (Smooth mode)." : "Animations disabled (Fast mode).");
        }
        app_host_invalidate(-1);
    } else if (eq(command, "perf")) app_host_perf_summary(output, (int)sizeof(output));
    else if (eq(command, "clear")) output[0] = 0;
    else if (eq(command, "tasks")) process_list(output);
    else if (eq(command, "uptime")) {
        char n[12];
        u32 sec = (ticks * 10) / 119;
        u32 mins = sec / 60;
        sec %= 60;
        copy(output, "Uptime: ");
        number(n, mins);
        append_str(output, n, 240);
        append_str(output, "m ", 240);
        number(n, sec);
        append_str(output, n, 240);
        append_str(output, "s (ticks: ", 240);
        number(n, ticks);
        append_str(output, n, 240);
        append_str(output, ")", 240);
    } else if (eq(command, "free") || eq(command, "mem")) {
        char n[12];
        u32 free_pages = pmm_get_free_pages_count();
        u32 total_pages = pmm_get_total_pages_count();
        u32 used_pages = total_pages > free_pages ? (total_pages - free_pages) : 0;
        copy(output, "Memory Summary:\n Total: ");
        number(n, (total_pages * 4));
        append_str(output, n, 240);
        append_str(output, " KiB\n Used:  ", 240);
        number(n, (used_pages * 4));
        append_str(output, n, 240);
        append_str(output, " KiB\n Free:  ", 240);
        number(n, (free_pages * 4));
        append_str(output, n, 240);
        append_str(output, " KiB (", 240);
        number(n, free_pages);
        append_str(output, n, 240);
        append_str(output, " pages)", 240);
    } else if (eq(command, "lspci")) {
        PciDevice devs[MAX_PCI_DEVICES];
        int num_devs = pci_scan_bus(devs, MAX_PCI_DEVICES);
        copy(output, "PCI Devices Detected:\n");
        for (int d = 0; d < num_devs && d < 6; d++) {
            char nb[12];
            number(nb, devs[d].dev);
            append_str(output, "00:", 240);
            append_str(output, nb, 240);
            append_str(output, " ", 240);
            append_str(output, pci_class_name(devs[d].class_code, devs[d].subclass), 240);
            append_str(output, "\n", 240);
        }
    } else if (eq(command, "beep")) {
        speaker_beep(880, 100);
        copy(output, "PC Speaker beeped at 880 Hz.");
    } else if (eq(command, "shutdown")) {
        if (notes_changed() && !notes_save()) copy(output, "Shutdown cancelled: note could not be saved.");
        else app_host_power(0);
    } else if (eq(command, "time") || eq(command, "date")) {
        char t_buf[16];
        rtc_format_time(t_buf, sizeof(t_buf));
        copy(output, "CMOS RTC Time: ");
        append_str(output, t_buf, 240);
    } else if (eq(command, "reboot")) {
        if (notes_changed() && !notes_save()) copy(output, "Reboot cancelled: note could not be saved.");
        else app_host_power(1);
    } else copy(output, "Unknown command. Type HELP.");
    serial("SHELL command executed\n");
    serial(output);
    serial("\n");
    serial("SHELL END\n");
    cmdlen = 0;
    command[0] = 0;
}
void terminal_key(u8 code, char ch, int control) {
    if (code == 28) { execute(); return; }
    app_edit_key(command, &cmdlen, 48, code, ch, control);
}
