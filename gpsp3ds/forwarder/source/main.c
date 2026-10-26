/*
 * gpSP 3D forwarder: a tiny HOME Menu app for one game.
 *
 * make-forwarders.sh builds one CIA per game from this program, with the
 * game's path in romfs:/path.txt. Started from the HOME Menu, it writes
 * that path to sdmc:/3ds/gpsp3d/launch.txt and jumps to the installed
 * gpSP 3D CIA, which plays the game and closes when you quit.
 *
 * The icon maker (made on the 3DS itself) instead fills in the slot below
 * in a copy of this program: the game, the emulator to start and the file
 * that emulator reads the game's path from. Its layout must stay as it is.
 */
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define GPSP3D_TITLE_ID 0x000400000BAC8100ULL
#define LAUNCH_PATH     "sdmc:/3ds/gpsp3d/launch.txt"

typedef struct
{
   char magic[16];      /* "GP3D-FWD-SLOT-1" */
   u64  target;         /* title ID to start (0: gpSP 3D) */
   char launch[128];    /* file the target reads the game path from */
   char path[512];      /* the game ("" : use romfs:/path.txt) */
} fwd_slot;

/* in .data (initialized, written below so it is kept) */
fwd_slot fwd_slot_data __attribute__((aligned(16))) = { "GP3D-FWD-SLOT-1", 0, "", "" };

static void fail(const char *msg)
{
   gfxInitDefault();
   consoleInit(GFX_BOTTOM, NULL);
   printf("\n  HOME Menu game icon\n\n  %s\n\n  Press START to go back.\n", msg);
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
   char path[512] = "", launch[128] = LAUNCH_PATH, dir[128];
   u64 target = GPSP3D_TITLE_ID;
   volatile fwd_slot *slot = &fwd_slot_data;
   char *p;
   FILE *f;
   static u8 param[0x300], hmac[0x20];

   if (slot->path[0])
   {
      memcpy(path, (const char*)slot->path, sizeof(path) - 1);
      if (slot->launch[0])
         memcpy(launch, (const char*)slot->launch, sizeof(launch) - 1);
      if (slot->target)
         target = slot->target;
   }
   else if (R_SUCCEEDED(romfsInit()))
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

   /* make the launch file's folders */
   snprintf(dir, sizeof(dir), "%s", launch);
   for (p = dir + 6; (p = strchr(p, '/')); p++)
   {
      *p = 0;
      mkdir(dir, 0777);
      *p = '/';
   }
   f = fopen(launch, "w");
   if (!f)
   {
      fail("Could not write to the SD card.");
      return 0;
   }
   fprintf(f, "%s\n", path);
   fclose(f);

   if (R_FAILED(APT_PrepareToDoApplicationJump(0, target, MEDIATYPE_SD)) ||
       R_FAILED(APT_DoApplicationJump(param, sizeof(param), hmac)))
   {
      remove(launch);
      fail("The emulator is not installed.\n  Install its CIA with FBI first.");
      return 0;
   }
   /* the system closes us and starts gpSP 3D */
   while (aptMainLoop())
      svcSleepThread(10 * 1000 * 1000);
   return 0;
}
