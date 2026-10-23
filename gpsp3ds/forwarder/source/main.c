/*
 * gpSP 3D forwarder: a tiny HOME Menu app for one game.
 *
 * make-forwarders.sh builds one CIA per game from this program, with the
 * game's path in romfs:/path.txt. Started from the HOME Menu, it writes
 * that path to sdmc:/3ds/gpsp3d/launch.txt and jumps to the installed
 * gpSP 3D CIA, which plays the game and closes when you quit.
 */
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define GPSP3D_TITLE_ID 0x000400000BAC8100ULL
#define LAUNCH_PATH     "sdmc:/3ds/gpsp3d/launch.txt"

static void fail(const char *msg)
{
   gfxInitDefault();
   consoleInit(GFX_BOTTOM, NULL);
   printf("\n  gpSP 3D forwarder\n\n  %s\n\n  Press START to go back.\n", msg);
   while (aptMainLoop())
   {
      hidScanInput();
      if (hidKeysDown() & KEY_START)
         break;
      gspWaitForVBlank();
   }
   gfxExit();
}

int main(void)
{
   char path[512] = "";
   FILE *f;
   static u8 param[0x300], hmac[0x20];

   if (R_SUCCEEDED(romfsInit()))
   {
      f = fopen("romfs:/path.txt", "r");
      if (f)
      {
         if (fgets(path, sizeof(path), f))
            path[strcspn(path, "\r\n")] = 0;
         fclose(f);
      }
      romfsExit();
   }
   if (!path[0])
   {
      fail("This forwarder has no game path.");
      return 0;
   }

   mkdir("sdmc:/3ds", 0777);
   mkdir("sdmc:/3ds/gpsp3d", 0777);
   f = fopen(LAUNCH_PATH, "w");
   if (!f)
   {
      fail("Could not write to the SD card.");
      return 0;
   }
   fprintf(f, "%s\n", path);
   fclose(f);

   if (R_FAILED(APT_PrepareToDoApplicationJump(0, GPSP3D_TITLE_ID, MEDIATYPE_SD)) ||
       R_FAILED(APT_DoApplicationJump(param, sizeof(param), hmac)))
   {
      remove(LAUNCH_PATH);
      fail("gpSP 3D is not installed.\n  Install gpsp3d.cia with FBI first.");
      return 0;
   }
   /* the system closes us and starts gpSP 3D */
   while (aptMainLoop())
      svcSleepThread(10 * 1000 * 1000);
   return 0;
}
