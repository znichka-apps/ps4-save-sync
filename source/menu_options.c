#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <mini18n.h>

#include "saves.h"
#include "menu.h"
#include "menu_gui.h"
#include "libfont.h"
#include "orbisPad.h"
#include "google_drive.h"
#include "google_replace.h"

static int google_panel;
static int google_help_page;
static int google_help_only;
static unsigned google_selection;
static size_t google_text_prefix(const char *text, size_t limit)
{
    size_t n = strlen(text);
    if (n <= limit) return n;
    n = limit;
    while (n && ((unsigned char)text[n] & 0xc0) == 0x80) n--;
    return n;
}
void google_drive_ui_upload(const char *game, const char *title, const char *directory, uint32_t user)
{
    if (!google_drive_upload_start(game,title,directory,user)) {
        show_message("Unable to start Google Drive backup."); return;
    }
    google_panel = 1;
}
static void google_text(const char *text, int y, int bottom)
{
    char line[73];
    while (*text && y <= bottom) {
        size_t n = google_text_prefix(text,64);
        const char *newline = memchr(text, '\n', n);
        if (newline) n = (size_t)(newline - text);
        else if (text[n] && text[n] != ' ') {
            size_t word = n;
            while (word && text[word] != ' ') word--;
            if (word) n = word;
        }
        memcpy(line, text, n); line[n] = 0;
        DrawString(180, y, line);
        y += 50; text += n;
        while (*text == ' ' || *text == '\n') text++;
    }
    if (*text) DrawString(180, bottom, "...");
}
static void google_help_line(int y, const char *line, int heading)
{
    SetFontSize(heading ? 44 : 39, heading ? 50 : 45);
    SetFontColor((heading ? APP_FONT_TITLE_COLOR : APP_FONT_COLOR) | 0xFF, 0);
    DrawString(155, y, line);
}
static void google_draw_help(void)
{
    DrawHeader(cat_opt_png_index, 0, "Google Drive: How to use", NULL,
        APP_FONT_TITLE_COLOR | 0xFF, 0xffffffff, 0);
    SetFontAlign(FONT_ALIGN_LEFT);
    if (google_help_page == 1) {
        google_help_line(205, "BACK UP AND RESTORE", 1);
        google_help_line(285, "1. Settings > Connect Google Drive. Follow the URL and code.", 0);
        google_help_line(360, "2. HDD Saves > select a save > Back up to Google Drive.", 0);
        google_help_line(435, "3. Open Google Drive on the main screen. Choose a backup.", 0);
        google_help_line(510, "4. Confirm to download. Checksum, ZIP and SFO are validated.", 0);
        google_help_line(585, "5. Save exists? Triangle: Replace. Do not delete it first.", 0);
        google_help_line(690, "6. No save yet? Cross: restore into the empty slot.", 0);
        google_help_line(765, "Backups stay in Drive; failed restores retain downloaded ZIPs.", 0);
    } else {
        google_help_line(205, "ACCOUNTS AND REPLACE", 1);
        google_help_line(275, "Both PS4 profiles need the same Apollo offline Account ID.", 0);
        google_help_line(340, "Local user IDs may differ. Use this app: User Tools > Activate PS4 Accounts.", 0);
        google_help_line(405, "No separate Apollo download or PSN sign-in is needed.", 0);
        google_help_line(490, "Replace was tested successfully; restored progress loaded.", 0);
        google_help_line(555, "Keep the console awake until the operation finishes.", 0);
        google_help_line(620, "Keep a separate backup; Replace removes the target after rollback upload.", 0);
        google_help_line(685, "Power loss can leave a missing or partial save; recovery is best effort.", 0);
        google_help_line(770, "Account ID lookup failed? Activate offline, reboot, retry.", 0);
        google_help_line(835, "Log: /data/ps4-save-sync/google_restore.log", 0);
    }
    SetFontSize(37, 43);
    SetFontColor(APP_FONT_COLOR | 0xFF, 0);
    DrawString(155, 975, "L1 / R1: page       Square / Back: close");
}
void google_drive_ui_help(void)
{
    google_help_only = !google_panel;
    google_panel = 1;
    google_help_page = 1;
}
void google_drive_ui_start(int action)
{
    google_selection = 0;
    google_help_page = 0;
    if (!google_drive_start(action, apollo_config.user_id)) {
        if (google_replace_pending(apollo_config.user_id,NULL)!=0) {
            google_panel=1;
            return;
        }
        show_message("Unable to start Google Drive operation.");
        return;
    }
    google_panel = 1;
}

int google_drive_ui_frame(void)
{
    google_drive_status status;
    google_backup pending_backup={0};
    if (!google_panel) return 0;
    if (google_help_page) {
        if (orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_R1)) google_help_page = 2;
        if (orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_L1)) google_help_page = 1;
        if (orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_SQUARE) ||
            orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_CIRCLE)) {
            google_draw_help();
            google_help_page = 0;
            if (google_help_only) google_panel = 0;
            google_help_only = 0;
            return 1;
        }
        google_draw_help();
        return 1;
    }
    google_drive_snapshot(&status);
    int pending=google_replace_pending(apollo_config.user_id,&pending_backup);
    if (!status.busy && orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_SQUARE)) {
        google_help_page = 1;
        google_draw_help();
        return 1;
    }
    if (!status.busy && !status.mount_blocked && pending==1 &&
        orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_R1)) {
        if (show_dialog(DIALOG_TYPE_YESNO,
            "Retry best-effort recovery of %s/%s for user %08x?\nThe target may be partial. The retained rollback ZIP will be used. Power loss can still interrupt recovery.",
            pending_backup.title,pending_backup.directory,apollo_config.user_id) &&
            !google_drive_start(GOOGLE_RECOVER,apollo_config.user_id))
            show_message("Unable to start recovery. Journal and ZIPs retained.");
        return 1;
    }
    if (!status.busy && !status.mount_blocked && pending==0 && status.restore_ready &&
        orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_CROSS)) {
        const google_backup *b=&status.restore_backup;
        if (show_dialog(DIALOG_TYPE_YESNO,"Restore into an EMPTY save slot?\nGame: %s\nTitle ID: %s\nSave directory: %s\nBackup UTC: %s\nCurrent PS4 user: %08x\nExisting saves will be refused.",
            b->game,b->title,b->directory,b->utc,apollo_config.user_id)) {
            if (!google_drive_start(GOOGLE_RESTORE,apollo_config.user_id)) show_message("Unable to start restore for this PS4 user.");
        } else google_drive_discard_download(apollo_config.user_id);
        /* Do not consume the same confirm/cancel again below this dialog. */
        return 1;
    }
    if (!status.busy && !status.mount_blocked && pending==0 && status.restore_ready &&
        orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_TRIANGLE)) {
        const google_backup *b=&status.restore_backup;
        if (show_dialog(DIALOG_TYPE_YESNO,
            "REPLACE existing save?\nGame: %s\nTitle ID: %s\nSave directory: %s\nUser: %08x\nA verified rollback will be uploaded first. Replacement is not atomic; power loss can leave the save missing or partial. Recovery is best effort.",
            b->game,b->title,b->directory,apollo_config.user_id) &&
            !google_drive_start(GOOGLE_REPLACE,apollo_config.user_id))
            show_message("Unable to start replacement for this PS4 user.");
        return 1;
    }
    if (!status.busy && status.browsing) {
        if (google_selection >= status.backups.count) google_selection = 0;
        if (orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_UP) && google_selection) google_selection--;
        if (orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_DOWN) && google_selection + 1 < status.backups.count) google_selection++;
        if (orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_R1) && status.backups.next[0]) google_drive_ui_start(GOOGLE_NEXT);
        if (orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_CROSS) && status.backups.count &&
            !google_drive_download_start(google_selection,apollo_config.user_id))
            show_message("Unable to start Google Drive download.");
    }
    if (orbisPadGetButtonPressed(ORBIS_PAD_BUTTON_CIRCLE)) {
        if (status.mount_blocked) { /* Require restart after an unconfirmed unmount. */ }
        else if (status.busy && status.cancellable) google_drive_cancel();
        else if (status.busy) { /* Let atomic credential updates finish. */ }
        else if (pending==1) google_panel=0;
        else if (status.restore_ready) {
            if (google_drive_discard_download(apollo_config.user_id)) google_panel=0;
        } else google_panel = 0;
    }
    DrawHeader(cat_opt_png_index, 0, "Google Drive", NULL, APP_FONT_TITLE_COLOR | 0xFF, 0xffffffff, 0);
    SetFontAlign(FONT_ALIGN_LEFT);
    SetFontSize(38, 44);
    SetFontColor(APP_FONT_COLOR | 0xFF, 0);
    /* Wrap sanitized text; reserve generous width for Google's returned values. */
    google_text(status.message, 220, status.browsing ? 300 : 375);
    const int show_details = !status.browsing && pending==0 &&
        !status.restore_ready && !status.verification_url[0];
    if (show_details && status.preparation_details[0])
        google_text(status.preparation_details, 460, status.discovery_details[0] ? 585 : 825);
    if (status.total_bytes)
        DrawFormatString(180, 390, "Transfer: %llu / %llu bytes (%u%%)",
            (unsigned long long)status.completed_bytes, (unsigned long long)status.total_bytes,
            (unsigned)(100.0 * status.completed_bytes / status.total_bytes));
    if (show_details && status.discovery_details[0])
        google_text(status.discovery_details, status.preparation_details[0] ? 625 : 460, 825);
    if (!status.busy && pending!=0) {
        char last_phase[112];
        if (!google_replace_last_phase(apollo_config.user_id,last_phase,sizeof(last_phase)))
            snprintf(last_phase,sizeof(last_phase),"unavailable");
        DrawString(180,790,"Last Replace phase:");
        google_text(last_phase,835,890);
    }
    if (!status.busy && pending==1) {
        DrawFormatString(180,690,"Recovery pending: %s / %s, user %08x",
            pending_backup.title,pending_backup.directory,apollo_config.user_id);
        DrawString(180,750,"Target may be partial. Both ZIPs and journal are retained.");
        DrawString(180,935,"R1: retry recovery    Back: close");
    } else if (!status.busy && pending<0) {
        DrawString(180,750,"Replacement journal cannot be verified. Save operations blocked.");
    }
    if (!status.busy && pending==0 && status.restore_ready) {
        const google_backup *b=&status.restore_backup;
        const int cross_ok=orbisPadGetConf()->crossButtonOK;
        const char *restore_button=cross_ok?"Cross":"Circle";
        const char *discard_button=cross_ok?"Circle":"Cross";
        DrawFormatString(180,670,"Game: %.*s",(int)google_text_prefix(b->game,65),b->game);
        DrawFormatString(180,720,"Title: %s  Save: %s",b->title,b->directory);
        DrawFormatString(180,770,"Backup UTC: %s  Current user: %08x",b->utc,apollo_config.user_id);
        DrawFormatString(180,875,"%s: empty slot    Triangle: replace    %s: discard",
            restore_button,discard_button);
    }
    if (!status.busy && status.browsing) {
        SetFontSize(34, 40);
        for (unsigned i=0;i<status.backups.count;i++) {
            const google_backup *b=&status.backups.entries[i].backup;
            /* All remote values are arguments, never format strings. */
            DrawFormatString(180,350+i*40,"%s [%s] %s  %.*s",i==google_selection?">":" ",b->title,b->utc,
                (int)google_text_prefix(b->game,42),b->game);
        }
        SetFontSize(36, 42);
        if (status.backups.count) {
            const google_backup *b=&status.backups.entries[google_selection].backup;
            DrawFormatString(180,775,"Game: %.*s",(int)google_text_prefix(b->game,65),b->game);
            DrawFormatString(180,820,"Save: %s",b->directory);
            DrawFormatString(180,865,"Backup: %s  Size: %llu bytes",b->utc,(unsigned long long)b->size);
        }
        DrawString(180,920,"Up/Down: select    Confirm: download    R1: next page");
    }
    if (status.verification_url[0]) {
        DrawString(180, 480, "Verification URL:");
        google_text(status.verification_url, 530, 665);
        DrawString(180, 710, "User code (case sensitive):");
        google_text(status.user_code, 760, 860);
    }
    const char *cancel_button = orbisPadGetConf()->crossButtonOK ? "Circle" : "Cross";
    if (status.mount_blocked)
        DrawString(180, 995, "Mount state uncertain. Stop save operations.");
    else if (status.busy && !status.cancellable)
        DrawString(180, 995, "Finishing operation...");
    else
        DrawFormatString(180, 995, "%s: %s", cancel_button,
            status.busy ? "cancel operation" : "return");
    if (!status.busy) DrawString(1120, 995, "Square: How to use");
    return 1;
}

static void _draw_OptionsMenu(u8 alpha)
{
	int c = 0;
	const int row_height = 51;
	SetFontSize(43, 47);
    for (int ind = 0, y_off = 180; menu_options[ind].name; ind++, y_off += row_height)
    {
        if (menu_options[ind].spacer)
			y_off += row_height / 2;

        SetFontColor(APP_FONT_COLOR | alpha, 0);
        DrawString(MENU_ICON_OFF + MENU_TITLE_OFF + 50, y_off, menu_options[ind].name);

		switch (menu_options[ind].type)
		{
			case APP_OPTION_BOOL:
				c = (*menu_options[ind].value == 1) ? opt_on_png_index : opt_off_png_index;
				DrawTexture(&menu_textures[c], OPTION_ITEM_OFF - 29, y_off, 0, menu_textures[c].width, menu_textures[c].height, 0xFFFFFF00 | alpha);
				break;

			case APP_OPTION_CALL:
				DrawTexture(&menu_textures[orbisPadGetConf()->crossButtonOK ? footer_ico_cross_png_index : footer_ico_circle_png_index], OPTION_ITEM_OFF - 29, y_off+2, 0, menu_textures[footer_ico_cross_png_index].width, menu_textures[footer_ico_cross_png_index].height, 0xFFFFFF00 | alpha);
				break;

			case APP_OPTION_LIST:
				SetFontAlign(FONT_ALIGN_CENTER);
				DrawFormatString(OPTION_ITEM_OFF - 18, y_off, "< %s >", menu_options[ind].options[*menu_options[ind].value]);
				SetFontAlign(FONT_ALIGN_LEFT);
				break;

			case APP_OPTION_INC:
				SetFontAlign(FONT_ALIGN_CENTER);
				DrawFormatString(OPTION_ITEM_OFF - 18, y_off, "- %d +", *menu_options[ind].value);
				SetFontAlign(FONT_ALIGN_LEFT);
				break;

			default:
				break;
		}
        
        if (menu_sel == ind)
        {
            DrawTexture(&menu_textures[mark_line_png_index], 0, y_off, 0, SCREEN_WIDTH, menu_textures[mark_line_png_index].height * 2, 0xFFFFFF00 | alpha);
			DrawTextureCenteredX(&menu_textures[mark_arrow_png_index], MENU_ICON_OFF + MENU_TITLE_OFF, y_off, 0, (2 * row_height) / 3, row_height + 2, 0xFFFFFF00 | alpha);
        }
    }
}

void Draw_OptionsMenu_Ani(void)
{
    int ani = 0;
    for (ani = 0; ani < MENU_ANI_MAX; ani++)
    {
        SDL_RenderClear(renderer);
        DrawHeader_Ani(cat_opt_png_index, _("Settings"), NULL, APP_FONT_TITLE_COLOR, 0xffffffff, ani, 12);
        
		u8 icon_a = (u8)(((ani * 2) > 0xFF) ? 0xFF : (ani * 2));
        int _game_a = (int)(icon_a - (MENU_ANI_MAX / 2)) * 2;
        if (_game_a > 0xFF)
            _game_a = 0xFF;
        u8 game_a = (u8)(_game_a < 0 ? 0 : _game_a);
        
        if (game_a > 0)
        	_draw_OptionsMenu(game_a);
        
        SDL_RenderPresent(renderer);
        
        if (game_a == 0xFF)
            return;
    }
}

void Draw_OptionsMenu(void)
{
    DrawHeader(cat_opt_png_index, 0, _("Settings"), NULL, APP_FONT_TITLE_COLOR | 0xFF, 0xffffffff, 0);
    _draw_OptionsMenu(0xFF);
}
