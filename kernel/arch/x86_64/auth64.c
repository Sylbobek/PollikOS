/* x86_64 console authentication using the existing i386 account.db wire format.
 * The data disk is never formatted or reset here. */
#include <stdint.h>
#include <stddef.h>
#include "auth64.h"
#include "../../account.h"
#include "../../security.h"
#include "tty.h"
#include "scheduler.h"
#include "process_internal.h"
#include "console_fb.h"
#include "mouse.h"
#include "../../pollikfs.h"
#include "../../vfs.h"
#include "../../hal.h"

typedef AccountRecord Account64;
static int elevation_error;
extern void kernel64_debug_bytes(const char *data,size_t length);

static size_t text_length(const char *text) {
    size_t n=0;
    while (text[n]) ++n;
    return n;
}
static void output(const char *text) { kernel64_debug_bytes(text,text_length(text)); }
static void zero(void *memory,size_t length) {
    volatile u8 *p=(volatile u8 *)memory;
    while (length--) *p++=0;
}
static int equal_secret(const u8 *a,const u8 *b,size_t length) {
    u8 difference=0;
    for (size_t i=0;i<length;i++) difference|=a[i]^b[i];
    return difference==0;
}
static int read_line(const char *prompt,char *buffer,size_t capacity,int secret) {
    size_t length=0;
    if (!capacity) return 0;
    zero(buffer,capacity);
    output(prompt);
    uint64_t blink=scheduler64_ticks()/50;
    if(secret==2)console_fb_elevation_draw(process64_elevation_name(),0,elevation_error,1);
    for (;;) {
        tty64_usb_poll();
        u8 key;
        int has_key=(int)tty64_pop(&key,1);
        if(secret==2){
            MouseEvent64 event;
            while(mouse64_pop(&event))if((event.kind&USER_INPUT_MOUSE_BUTTON)&&(event.changed&USER_MOUSE_BUTTON_LEFT)&&(event.buttons&USER_MOUSE_BUTTON_LEFT)){
                int action=console_fb_elevation_button(event.x,event.y);if(action){key=action==1?27:'\r';has_key=1;}
            }
            uint64_t now=scheduler64_ticks()/50;if(now!=blink){blink=now;console_fb_elevation_draw(process64_elevation_name(),(unsigned)length,elevation_error,(int)(!(now&1)));}
        }
        if (!has_key) {
            hal_cpu_idle_once_disabled();
            continue;
        }
        account_entropy_event(account_platform_time());
        if(key==27 && secret==2){zero(buffer,capacity);output("Cancelled.\r\n");return 0;}
        if (key=='\r' || key=='\n') {
            output("\r\n");
            buffer[length]=0;
            return 1;
        }
        if ((key==8 || key==127) && length) {
            buffer[--length]=0;
            if (!secret) output("\b \b");
            if(secret==2)console_fb_elevation_draw(process64_elevation_name(),(unsigned)length,elevation_error,1);
            continue;
        }
        if (key>=32 && key<127 && length+1<capacity) {
            buffer[length++]=(char)key;
            if (!secret) kernel64_debug_bytes((const char *)&key,1);
            else output("*");
            if(secret==2){elevation_error=0;console_fb_elevation_draw(process64_elevation_name(),(unsigned)length,0,1);}
        }
    }
}
static int valid_username(const char *name) { return account_valid_username(name); }
static int create_account(Account64 *a,const char *name,const char *password) { return account_create(a,name,password); }
static int setup_account(Account64 *account) {
    char username[32],password[64],confirmation[64];
    for (;;) {
        read_line("Create account name: ",username,sizeof(username),0);
        if (valid_username(username)) break;
        output("Use 2-31 lowercase letters, numbers, '-' or '_'.\r\n");
    }
    for (;;) {
        for (;;) {
            read_line("Create password (6-63 characters): ",password,sizeof(password),1);
            if (text_length(password)>=6) break;
            output("Password must contain at least 6 characters.\r\n");
        }
        read_line("Confirm password: ",confirmation,sizeof(confirmation),1);
        if (equal_secret((const u8 *)password,(const u8 *)confirmation,sizeof(password))) break;
        output("Passwords do not match. Try again.\r\n");
        zero(password,sizeof(password));
        zero(confirmation,sizeof(confirmation));
    }
    int saved=create_account(account,username,password);
    zero(password,sizeof(password));
    zero(confirmation,sizeof(confirmation));
    zero(username,sizeof(username));
    if (!saved) { output("Could not save the account; data was not formatted.\r\n"); return 0; }
    output("Account created.\r\n");
    return 1;
}
int auth64_login(void) {
    Account64 account;
    if (!pollikfs_mounted()) {
        output("[AUTH64] Data filesystem unavailable; login denied. No formatting was performed.\r\n");
        return 0;
    }
    int loaded=account_load(&account);
    if (loaded<0) {
        output("[AUTH64] Account database is invalid; login denied. Data was left untouched.\r\n");
        return 0;
    }
    if (!loaded) {
        output("[AUTH64] First run: create a local account.\r\n");
        int created=setup_account(&account);
        if(created) security_session_begin(account.username);
        zero(&account,sizeof(account));
        return created;
    }
    output("[AUTH64] Sign in to PollikOS.\r\n");
    for (unsigned attempt=0;attempt<5;attempt++) {
        char username[32],password[64];
        read_line("Username: ",username,sizeof(username),0);
        read_line("Password: ",password,sizeof(password),1);
        int accepted=equal_secret((const u8 *)username,(const u8 *)account.username,sizeof(username)) &&
                     account_verify(&account,password);
        zero(password,sizeof(password));
        zero(username,sizeof(username));
        if (accepted) {
            output("[AUTH64] Sign-in successful.\r\n");
            if(security_session_state()==SESSION_NONE) security_session_begin(account.username);
            else security_session_resume();
            zero(&account,sizeof(account));
            return 1;
        }
        output("Sign-in failed.\r\n");
        uint64_t until=scheduler64_ticks()+100u*(attempt+1);
        while(scheduler64_ticks()<until) hal_cpu_idle_once_disabled();
    }
    output("[AUTH64] Too many failed attempts; restarting the kernel is required.\r\n");
    zero(&account,sizeof(account));
    return 0;
}

int auth64_change_password(void) {
    Account64 account; char old[64],password[64],confirm[64];
    if(account_load(&account)!=1) return 0;
    read_line("Current password: ",old,sizeof(old),1);
    read_line("New password (6-63 characters): ",password,sizeof(password),1);
    read_line("Confirm new password: ",confirm,sizeof(confirm),1);
    int ok=equal_secret((const u8 *)password,(const u8 *)confirm,64) && account_change(&account,old,password);
    zero(old,sizeof(old)); zero(password,sizeof(password)); zero(confirm,sizeof(confirm)); zero(&account,sizeof(account));
    output(ok?"[AUTH64] Password changed.\r\n":"[AUTH64] Password change failed.\r\n");
    security_session_resume(); return ok;
}
int auth64_elevate(void) {
    static uint64_t failed_session;
    static unsigned failures;
    if(failed_session!=security_session_id()){failed_session=security_session_id();failures=0;}
    if(failures>=5){output("[AUTH64] Administrator launch locked. Sign out to retry.\r\n");return 0;}
    Account64 account;char password[64];
    output("[AUTH64] Run as administrator. Full application permissions.\r\n");
    if(account_load(&account)!=1)return 0;
    int accepted=0;
    elevation_error=0;
    for(unsigned attempt=0;failures<5;attempt++) {
        if(!read_line("Administrator password (Esc cancels): ",password,sizeof(password),2))break;
        accepted=account_verify(&account,password);
        zero(password,sizeof(password));
        if(accepted){failures=0;break;}
        failures++;
        elevation_error=1;
        output("Incorrect password.\r\n");
        uint64_t until=scheduler64_ticks()+100u*(attempt+1);
        while(scheduler64_ticks()<until)hal_cpu_idle_once_disabled();
    }
    zero(password,sizeof(password));zero(&account,sizeof(account));
    output(accepted?"[AUTH64] Administrator launch authorized.\r\n":"[AUTH64] Administrator launch denied.\r\n");
    return accepted;
}
