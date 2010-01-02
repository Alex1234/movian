/*
 * Copyright (C) 2000, 2001 H�kan Hjort
 * Copyright (C) 2001 Rich Wareham <richwareham@users.sourceforge.net>
 *               2002-2004 the dvdnav project
 * 
 * This file is part of libdvdnav, a DVD navigation library. It is modified
 * from a file originally part of the Ogle DVD player.
 * 
 * libdvdnav is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * libdvdnav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: vm.c 1092 2008-06-08 09:03:10Z nicodvb $
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <limits.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>

#include <libdvdread/nav_types.h>
#include <libdvdread/ifo_types.h>
#include <libdvdread/ifo_read.h>
#include "dvd_types.h"

#include "decoder.h"
#include "remap.h"
#include "vm.h"
#include "dvdnav.h"
#include "dvdnav_internal.h"

#include "showtime.h"

#ifdef _MSC_VER
#include <io.h>   /* read() */
#endif /* _MSC_VER */

/*
#define STRICT
*/

/* Local prototypes */

/* get_XYZ returns a value.
 * set_XYZ sets state using passed parameters.
 *         returns success/failure.
 */

/* Play */
static link_t play_PGC(vm_t *vm);
static link_t play_PGC_PG(vm_t *vm, int pgN);
static link_t play_PGC_post(vm_t *vm);
static link_t play_PG(vm_t *vm);
static link_t play_Cell(vm_t *vm);
static link_t play_Cell_post(vm_t *vm);

/* Process link - returns 1 if a hop has been performed */
static int process_command(vm_t *vm,link_t link_values);

/* Set */
static int  set_TT(vm_t *vm, int tt);
static int  set_PTT(vm_t *vm, int tt, int ptt);
static int  set_VTS_TT(vm_t *vm, int vtsN, int vts_ttn);
static int  set_VTS_PTT(vm_t *vm, int vtsN, int vts_ttn, int part);
static int  set_FP_PGC(vm_t *vm);
static int  set_MENU(vm_t *vm, int menu);
static int  set_PGCN(vm_t *vm, int pgcN);
static int  set_PGN(vm_t *vm); /* Set PGN based on (vm->state).CellN */
static void set_RSMinfo(vm_t *vm, int cellN, int blockN);

/* Get */
static int get_TT(vm_t *vm, int vtsN, int vts_ttn);
static int get_ID(vm_t *vm, int id);
static int get_PGCN(vm_t *vm);

static pgcit_t* get_MENU_PGCIT(vm_t *vm, ifo_handle_t *h, uint16_t lang);
static pgcit_t* get_PGCIT(vm_t *vm);


/* Helper functions */

static void vm_print_current_domain_state(vm_t *vm) {
  switch((vm->state).domain) {
    case VTS_DOMAIN:
      TRACE(TRACE_DEBUG, "DVDNAV", "Video Title Domain: -");
      break;

    case VTSM_DOMAIN:
      TRACE(TRACE_DEBUG, "DVDNAV", "Video Title Menu Domain: -");
      break;

    case VMGM_DOMAIN:
      TRACE(TRACE_DEBUG, "DVDNAV", "Video Manager Menu Domain: -");
      break;

    case FP_DOMAIN: 
      TRACE(TRACE_DEBUG, "DVDNAV", "First Play Domain: -");
      break;

    default:
      TRACE(TRACE_DEBUG, "DVDNAV", "Unknown Domain: -");
      break;
  }
  TRACE(TRACE_DEBUG, "DVDNAV", "VTS:%d PGC:%d PG:%u CELL:%u BLOCK:%u VTS_TTN:%u TTN:%u TT_PGCN:%u", 
                   (vm->state).vtsN,
                   get_PGCN(vm),
                   (vm->state).pgN,
                   (vm->state).cellN,
                   (vm->state).blockN,
                   (vm->state).VTS_TTN_REG,
                   (vm->state).TTN_REG,
                   (vm->state).TT_PGCN_REG);
}

static void dvd_read_name(char *name, const char *device) {
    /* Because we are compiling with _FILE_OFFSET_BITS=64
     * all off_t are 64bit.
     */
    off_t off;
    int fd, i;
    uint8_t data[DVD_VIDEO_LB_LEN];

    /* Read DVD name */
    fd = open(device, O_RDONLY);
    if (fd > 0) { 
      off = lseek( fd, 32 * (off_t) DVD_VIDEO_LB_LEN, SEEK_SET );
      if( off == ( 32 * (off_t) DVD_VIDEO_LB_LEN ) ) {
        off = read( fd, data, DVD_VIDEO_LB_LEN ); 
        close(fd);
        if (off == ( (off_t) DVD_VIDEO_LB_LEN )) {
#if 0
          for(i=25; i < 73; i++ ) {
            if((data[i] == 0)) break;
            if((data[i] > 32) && (data[i] < 127)) {
	      TRACE(TRACE_INFO, "DVDNAV", "DVD Title: %s", data[i]);
            }
          }
          strncpy(name, (char*) &data[25], 48);
          name[48] = 0;
          fprintf(MSG_OUT, "\nlibdvdnav: DVD Serial Number: ");
          for(i=73; i < 89; i++ ) {
            if((data[i] == 0)) break;
            if((data[i] > 32) && (data[i] < 127)) {
              fprintf(MSG_OUT, "%c", data[i]);
            } else {
              fprintf(MSG_OUT, " ");
            } 
          }
          fprintf(MSG_OUT, "\nlibdvdnav: DVD Title (Alternative): ");
          for(i=89; i < 128; i++ ) {
            if((data[i] == 0)) break;
            if((data[i] > 32) && (data[i] < 127)) {
              fprintf(MSG_OUT, "%c", data[i]);
            } else {
              fprintf(MSG_OUT, " ");
            }
          }
          fprintf(MSG_OUT, "");
#endif
        } else {
          TRACE(TRACE_ERROR, "DVDNAV", "Can't read name block. Probably not a DVD-ROM device.");
        }
      } else {
        TRACE(TRACE_ERROR, "DVDNAV", "Can't seek to block %u", 32 );
      }
      close(fd);
    } else {
      TRACE(TRACE_ERROR, "DVDNAV", "NAME OPEN FAILED");
  }
}

static int ifoOpenNewVTSI(vm_t *vm, dvd_reader_t *dvd, int vtsN) {
  if((vm->state).vtsN == vtsN) {
    return 1; /*  We alread have it */
  }
  
  if(vm->vtsi != NULL)
    ifoClose(vm->vtsi);
  
  vm->vtsi = ifoOpenVTSI(dvd, vtsN);
  if(vm->vtsi == NULL) {
    TRACE(TRACE_ERROR, "DVDNAV", "ifoOpenVTSI failed");
    return 0;
  }
  if(!ifoRead_VTS_PTT_SRPT(vm->vtsi)) {
    TRACE(TRACE_ERROR, "DVDNAV", "ifoRead_VTS_PTT_SRPT failed");
    return 0;
  }
  if(!ifoRead_PGCIT(vm->vtsi)) {
    TRACE(TRACE_ERROR, "DVDNAV", "ifoRead_PGCIT failed");
    return 0;
  }
  if(!ifoRead_PGCI_UT(vm->vtsi)) {
    TRACE(TRACE_ERROR, "DVDNAV", "ifoRead_PGCI_UT failed");
    return 0;
  }
  if(!ifoRead_VOBU_ADMAP(vm->vtsi)) {
    TRACE(TRACE_ERROR, "DVDNAV", "ifoRead_VOBU_ADMAP vtsi failed");
    return 0;
  }
  if(!ifoRead_TITLE_VOBU_ADMAP(vm->vtsi)) {
    TRACE(TRACE_ERROR, "DVDNAV", "ifoRead_TITLE_VOBU_ADMAP vtsi failed");
    return 0;
  }
  (vm->state).vtsN = vtsN;
  
  return 1;
}


/* Initialisation & Destruction */

vm_t* vm_new_vm() {
  return (vm_t*)calloc(sizeof(vm_t), sizeof(char));
}

void vm_free_vm(vm_t *vm) {
  vm_stop(vm);
  free(vm);
}


/* IFO Access */

ifo_handle_t *vm_get_vmgi(vm_t *vm) {
  return vm->vmgi;
}

ifo_handle_t *vm_get_vtsi(vm_t *vm) {
  return vm->vtsi;
}


/* Reader Access */

dvd_reader_t *vm_get_dvd_reader(vm_t *vm) {
  return vm->dvd;
}


/* Basic Handling */

int vm_start(vm_t *vm) {
  /* Set pgc to FP (First Play) pgc */
  set_FP_PGC(vm);
  process_command(vm, play_PGC(vm));
  return !vm->stopped;
}

void vm_stop(vm_t *vm) {
  if(vm->vmgi) {
    ifoClose(vm->vmgi);
    vm->vmgi=NULL;
  }
  if(vm->vtsi) {
    ifoClose(vm->vtsi);
    vm->vtsi=NULL;
  }
  if(vm->dvd) {
    DVDClose(vm->dvd);
    vm->dvd=NULL;
  }
  vm->stopped = 1;
}
 
int vm_reset(vm_t *vm, const char *dvdroot, void *svfs_ops) {
  /*  Setup State */
  memset((vm->state).registers.SPRM, 0, sizeof((vm->state).registers.SPRM));
  memset((vm->state).registers.GPRM, 0, sizeof((vm->state).registers.GPRM));
  memset((vm->state).registers.GPRM_mode, 0, sizeof((vm->state).registers.GPRM_mode));
  memset((vm->state).registers.GPRM_mode, 0, sizeof((vm->state).registers.GPRM_mode));
  memset((vm->state).registers.GPRM_time, 0, sizeof((vm->state).registers.GPRM_time));
  (vm->state).registers.SPRM[0]  = ('e'<<8)|'n'; /* Player Menu Languange code */
  (vm->state).AST_REG            = 15;           /* 15 why? */
  (vm->state).SPST_REG           = 62;           /* 62 why? */
  (vm->state).AGL_REG            = 1;
  (vm->state).TTN_REG            = 1;
  (vm->state).VTS_TTN_REG        = 1;
  /* (vm->state).TT_PGCN_REG        = 0 */
  (vm->state).PTTN_REG           = 1;
  (vm->state).HL_BTNN_REG        = 1 << 10;
  (vm->state).PTL_REG            = 15;           /* Parental Level */
  (vm->state).registers.SPRM[12] = ('U'<<8)|'S'; /* Parental Management Country Code */
  (vm->state).registers.SPRM[16] = ('e'<<8)|'n'; /* Initial Language Code for Audio */
  (vm->state).registers.SPRM[18] = ('e'<<8)|'n'; /* Initial Language Code for Spu */
  (vm->state).registers.SPRM[20] = 0x1;          /* Player Regional Code Mask. Region free! */
  (vm->state).registers.SPRM[14] = 0x100;        /* Try Pan&Scan */
   
  (vm->state).pgN                = 0;
  (vm->state).cellN              = 0;
  (vm->state).cell_restart       = 0;

  (vm->state).domain             = FP_DOMAIN;
  (vm->state).rsm_vtsN           = 0;
  (vm->state).rsm_cellN          = 0;
  (vm->state).rsm_blockN         = 0;
  
  (vm->state).vtsN               = -1;
  
  if (vm->dvd && dvdroot) {
    /* a new dvd device has been requested */
    vm_stop(vm);
  }
  if (!vm->dvd) {
      vm->dvd = DVDOpen(dvdroot, svfs_ops);
    if(!vm->dvd) {
      TRACE(TRACE_ERROR, "DVDNAV", "vm: failed to open/read the DVD");
      return 0;
    }
    dvd_read_name(vm->dvd_name, dvdroot);
    vm->map  = remap_loadmap(vm->dvd_name);
    vm->vmgi = ifoOpenVMGI(vm->dvd);
    if(!vm->vmgi) {
      TRACE(TRACE_ERROR, "DVDNAV", "vm: failed to read VIDEO_TS.IFO");
      return 0;
    }
    if(!ifoRead_FP_PGC(vm->vmgi)) {
      TRACE(TRACE_ERROR, "DVDNAV", "vm: ifoRead_FP_PGC failed");
      return 0;
    }
    if(!ifoRead_TT_SRPT(vm->vmgi)) {
      TRACE(TRACE_ERROR, "DVDNAV", "vm: ifoRead_TT_SRPT failed");
      return 0;
    }
    if(!ifoRead_PGCI_UT(vm->vmgi)) {
      TRACE(TRACE_ERROR, "DVDNAV", "vm: ifoRead_PGCI_UT failed");
      return 0;
    }
    if(!ifoRead_PTL_MAIT(vm->vmgi)) {
      TRACE(TRACE_ERROR, "DVDNAV", "vm: ifoRead_PTL_MAIT failed");
      /* return 0; Not really used for now.. */
    }
    if(!ifoRead_VTS_ATRT(vm->vmgi)) {
      TRACE(TRACE_ERROR, "DVDNAV", "vm: ifoRead_VTS_ATRT failed");
      /* return 0; Not really used for now.. */
    }
    if(!ifoRead_VOBU_ADMAP(vm->vmgi)) {
      TRACE(TRACE_ERROR, "DVDNAV", "vm: ifoRead_VOBU_ADMAP vgmi failed");
      /* return 0; Not really used for now.. */
    }
    /* ifoRead_TXTDT_MGI(vmgi); Not implemented yet */
  }
  if (vm->vmgi) {
    int i, mask;
    TRACE(TRACE_DEBUG, "DVDNAV", "DVD disk reports itself with Region mask 0x%08x. Regions:",
      vm->vmgi->vmgi_mat->vmg_category);
    for (i = 1, mask = 1; i <= 8; i++, mask <<= 1)
      if (((vm->vmgi->vmgi_mat->vmg_category >> 16) & mask) == 0)
        TRACE(TRACE_DEBUG, "DVDNAV", "Region: %d", i);
  }
  return 1;
}


/* copying and merging */

vm_t *vm_new_copy(vm_t *source) {
  vm_t *target = vm_new_vm();
  int vtsN;
  int pgcN = get_PGCN(source);
  int pgN  = (source->state).pgN;
  
  assert(pgcN);
  
  memcpy(target, source, sizeof(vm_t));
  
  /* open a new vtsi handle, because the copy might switch to another VTS */
  target->vtsi = NULL;
  vtsN = (target->state).vtsN;
  if (vtsN > 0) {
    (target->state).vtsN = 0;
    if (!ifoOpenNewVTSI(target, target->dvd, vtsN))
      assert(0);
  
    /* restore pgc pointer into the new vtsi */
    if (!set_PGCN(target, pgcN))
      assert(0);
    (target->state).pgN = pgN;
  }
  
  return target;
}

void vm_merge(vm_t *target, vm_t *source) {
  if(target->vtsi)
    ifoClose(target->vtsi);
  memcpy(target, source, sizeof(vm_t));
  memset(source, 0, sizeof(vm_t));
}

void vm_free_copy(vm_t *vm) {
  if(vm->vtsi)
    ifoClose(vm->vtsi);
  free(vm);
}


/* regular playback */

void vm_position_get(vm_t *vm, vm_position_t *position) {
  position->button = (vm->state).HL_BTNN_REG >> 10;
  position->vts = (vm->state).vtsN; 
  position->domain = (vm->state).domain; 
  position->spu_channel = (vm->state).SPST_REG;
  position->audio_channel = (vm->state).AST_REG;
  position->angle_channel = (vm->state).AGL_REG;
  position->hop_channel = vm->hop_channel; /* Increases by one on each hop */
  position->cell = (vm->state).cellN;
  position->cell_restart = (vm->state).cell_restart;
  position->cell_start = (vm->state).pgc->cell_playback[(vm->state).cellN - 1].first_sector;
  position->still = (vm->state).pgc->cell_playback[(vm->state).cellN - 1].still_time;
  position->block = (vm->state).blockN;

  /* handle PGC stills at PGC end */
  if ((vm->state).cellN == (vm->state).pgc->nr_of_cells)
    position->still += (vm->state).pgc->still_time;
  /* still already determined */
  if (position->still)
    return;
  /* This is a rough fix for some strange still situations on some strange DVDs.
   * There are discs (like the German "Back to the Future" RC2) where the only
   * indication of a still is a cell playback time higher than the time the frames
   * in this cell actually take to play (like 1 frame with 1 minute playback time).
   * On the said BTTF disc, for these cells last_sector and last_vobu_start_sector
   * are equal and the cells are very short, so we abuse these conditions to
   * detect such discs. I consider these discs broken, so the fix is somewhat
   * broken, too. */
  if (((vm->state).pgc->cell_playback[(vm->state).cellN - 1].last_sector ==
       (vm->state).pgc->cell_playback[(vm->state).cellN - 1].last_vobu_start_sector) &&
      ((vm->state).pgc->cell_playback[(vm->state).cellN - 1].last_sector -
       (vm->state).pgc->cell_playback[(vm->state).cellN - 1].first_sector < 1024)) {
    int time;
    int size = (vm->state).pgc->cell_playback[(vm->state).cellN - 1].last_sector -
	       (vm->state).pgc->cell_playback[(vm->state).cellN - 1].first_sector;
    time  = ((vm->state).pgc->cell_playback[(vm->state).cellN - 1].playback_time.hour   >> 4  ) * 36000;
    time += ((vm->state).pgc->cell_playback[(vm->state).cellN - 1].playback_time.hour   & 0x0f) * 3600;
    time += ((vm->state).pgc->cell_playback[(vm->state).cellN - 1].playback_time.minute >> 4  ) * 600;
    time += ((vm->state).pgc->cell_playback[(vm->state).cellN - 1].playback_time.minute & 0x0f) * 60;
    time += ((vm->state).pgc->cell_playback[(vm->state).cellN - 1].playback_time.second >> 4  ) * 10;
    time += ((vm->state).pgc->cell_playback[(vm->state).cellN - 1].playback_time.second & 0x0f) * 1;
    if (!time || size / time > 30)
      /* datarate is too high, it might be a very short, but regular cell */
      return;
    if (time > 0xff) time = 0xff;
    position->still = time;
  }
}

void vm_get_next_cell(vm_t *vm) {
  process_command(vm, play_Cell_post(vm));
}


/* Jumping */

int vm_jump_pg(vm_t *vm, int pg) {
  (vm->state).pgN = pg;
  process_command(vm, play_PG(vm));
  return 1;
}

int vm_jump_cell_block(vm_t *vm, int cell, int block) {
  (vm->state).cellN = cell;
  process_command(vm, play_Cell(vm));
  /* play_Cell can jump to a different cell in case of angles */
  if ((vm->state).cellN == cell)
    (vm->state).blockN = block;
  return 1;
}

int vm_jump_title_part(vm_t *vm, int title, int part) {
  link_t link;
  
  if(!set_PTT(vm, title, part))
    return 0;
  /* Some DVDs do not want us to jump directly into a title and have
   * PGC pre commands taking us back to some menu. Since we do not like that,
   * we do not execute PGC pre commands that would do a jump. */
  /* process_command(vm, play_PGC_PG(vm, (vm->state).pgN)); */
  link = play_PGC_PG(vm, (vm->state).pgN);
  if (link.command != PlayThis)
    /* jump occured -> ignore it and play the PG anyway */
    process_command(vm, play_PG(vm));
  else
    process_command(vm, link);
  return 1;
}

int vm_jump_top_pg(vm_t *vm) {
  process_command(vm, play_PG(vm));
  return 1;
}

int vm_jump_next_pg(vm_t *vm) {
  if((vm->state).pgN >= (vm->state).pgc->nr_of_programs) {
    /* last program -> move to TailPGC */
    process_command(vm, play_PGC_post(vm));
    return 1;
  } else {
    vm_jump_pg(vm, (vm->state).pgN + 1);
    return 1;
  }
}

int vm_jump_prev_pg(vm_t *vm) {
  if ((vm->state).pgN <= 1) {
    /* first program -> move to last program of previous PGC */
    if ((vm->state).pgc->prev_pgc_nr && set_PGCN(vm, (vm->state).pgc->prev_pgc_nr)) {
      process_command(vm, play_PGC(vm));
      vm_jump_pg(vm, (vm->state).pgc->nr_of_programs);
      return 1;
    }
    return 0;
  } else {
    vm_jump_pg(vm, (vm->state).pgN - 1);
    return 1;
  }
}

int vm_jump_up(vm_t *vm) {
  if((vm->state).pgc->goup_pgc_nr && set_PGCN(vm, (vm->state).pgc->goup_pgc_nr)) {
    process_command(vm, play_PGC(vm));
    return 1;
  }
  return 0;
}

int vm_jump_menu(vm_t *vm, DVDMenuID_t menuid) {
  domain_t old_domain = (vm->state).domain;
  
  switch ((vm->state).domain) {
  case VTS_DOMAIN:
    set_RSMinfo(vm, 0, (vm->state).blockN);
    /* FALL THROUGH */
  case VTSM_DOMAIN:
  case VMGM_DOMAIN:
    switch(menuid) {
    case DVD_MENU_Title:
    case DVD_MENU_Escape:
      (vm->state).domain = VMGM_DOMAIN;
      break;
    case DVD_MENU_Root:
    case DVD_MENU_Subpicture:
    case DVD_MENU_Audio:
    case DVD_MENU_Angle:
    case DVD_MENU_Part:
      (vm->state).domain = VTSM_DOMAIN;
      break;
    }
    if(get_PGCIT(vm) && set_MENU(vm, menuid)) {
      process_command(vm, play_PGC(vm));
      return 1;  /* Jump */
    } else {
      (vm->state).domain = old_domain;
    }
    break;
  case FP_DOMAIN: /* FIXME XXX $$$ What should we do here? */
    break;
  }
  
  return 0;
}

int vm_jump_resume(vm_t *vm) {
  link_t link_values = { LinkRSM, 0, 0, 0 };

  if (!(vm->state).rsm_vtsN) /* Do we have resume info? */
    return 0;
  if (!process_command(vm, link_values))
    return 0;
  return 1;
}

int vm_exec_cmd(vm_t *vm, vm_cmd_t *cmd) {
  link_t link_values;
  
  if(vmEval_CMD(cmd, 1, &(vm->state).registers, &link_values))
    return process_command(vm, link_values);
  else
    return 0; /*  It updated some state thats all... */
}


/* getting information */

int vm_get_current_menu(vm_t *vm, int *menuid) {
  pgcit_t* pgcit;
  int pgcn;
  pgcn = (vm->state).pgcN;
  pgcit = get_PGCIT(vm);
  if(pgcit==NULL) return 0;
  *menuid = pgcit->pgci_srp[pgcn - 1].entry_id & 0xf ;
  return 1;
}

int vm_get_current_title_part(vm_t *vm, int *title_result, int *part_result) {
  vts_ptt_srpt_t *vts_ptt_srpt;
  int title, part = 0, vts_ttn;
  int found;
  int16_t pgcN, pgN;

  vts_ptt_srpt = vm->vtsi->vts_ptt_srpt;
  pgcN = get_PGCN(vm);
  pgN = vm->state.pgN;

  found = 0;
  for (vts_ttn = 0; (vts_ttn < vts_ptt_srpt->nr_of_srpts) && !found; vts_ttn++) {
    for (part = 0; (part < vts_ptt_srpt->title[vts_ttn].nr_of_ptts) && !found; part++) {
      if (vts_ptt_srpt->title[vts_ttn].ptt[part].pgcn == pgcN) {
	if (vts_ptt_srpt->title[vts_ttn].ptt[part].pgn  == pgN) {
	  found = 1;
          break;
	}
	if (part > 0 && vts_ptt_srpt->title[vts_ttn].ptt[part].pgn > pgN &&
	    vts_ptt_srpt->title[vts_ttn].ptt[part - 1].pgn < pgN) {
	  part--;
	  found = 1;
	  break;
	}
      }
    }
    if (found) break;
  }
  vts_ttn++;
  part++;
  
  if (!found) {
    TRACE(TRACE_ERROR, "DVDNAV", "chapter NOT FOUND!");
    return 0;
  }

  title = get_TT(vm, vm->state.vtsN, vts_ttn);

  if (title) {
    TRACE(TRACE_DEBUG, "DVDNAV", "************ this chapter FOUND!");
    TRACE(TRACE_DEBUG, "DVDNAV", "VTS_PTT_SRPT - Title %3i part %3i: PGC: %3i PG: %3i",
             title, part,
             vts_ptt_srpt->title[vts_ttn-1].ptt[part-1].pgcn ,
             vts_ptt_srpt->title[vts_ttn-1].ptt[part-1].pgn );
  }
  *title_result = title;
  *part_result = part;
  return 1;
}

/* Return the substream id for 'logical' audio stream audioN.
 * 0 <= audioN < 8
 */
int vm_get_audio_stream(vm_t *vm, int audioN) {
  int streamN = -1;

  if((vm->state).domain != VTS_DOMAIN)
    audioN = 0;
  
  if(audioN < 8) {
    /* Is there any control info for this logical stream */ 
    if((vm->state).pgc->audio_control[audioN] & (1<<15)) {
      streamN = ((vm->state).pgc->audio_control[audioN] >> 8) & 0x07;  
    }
  }
  
  if((vm->state).domain != VTS_DOMAIN && streamN == -1)
    streamN = 0;
  
  /* FIXME: Should also check in vtsi/vmgi status what kind of stream
   * it is (ac3/lpcm/dts/sdds...) to find the right (sub)stream id */
  return streamN;
}

/* Return the substream id for 'logical' subpicture stream subpN and given mode.
 * 0 <= subpN < 32
 * mode == 0 - widescreen
 * mode == 1 - letterbox
 * mode == 2 - pan&scan
 */
int vm_get_subp_stream(vm_t *vm, int subpN, int mode) {
  int streamN = -1;
  int source_aspect = vm_get_video_aspect(vm);
  
  if((vm->state).domain != VTS_DOMAIN)
    subpN = 0;
  
  if(subpN < 32) { /* a valid logical stream */
    /* Is this logical stream present */ 
    if((vm->state).pgc->subp_control[subpN] & (1<<31)) {
      if(source_aspect == 0) /* 4:3 */	     
	streamN = ((vm->state).pgc->subp_control[subpN] >> 24) & 0x1f;  
      if(source_aspect == 3) /* 16:9 */
        switch (mode) {
	case 0:
	  streamN = ((vm->state).pgc->subp_control[subpN] >> 16) & 0x1f;
	  break;
	case 1:
	  streamN = ((vm->state).pgc->subp_control[subpN] >> 8) & 0x1f;
	  break;
	case 2:
	  streamN = (vm->state).pgc->subp_control[subpN] & 0x1f;
	}
    }
  }
  
  if((vm->state).domain != VTS_DOMAIN && streamN == -1)
    streamN = 0;

  /* FIXME: Should also check in vtsi/vmgi status what kind of stream it is. */
  return streamN;
}

int vm_get_audio_active_stream(vm_t *vm) {
  int audioN;
  int streamN;
  audioN = (vm->state).AST_REG ;
  streamN = vm_get_audio_stream(vm, audioN);
  
  /* If no such stream, then select the first one that exists. */
  if(streamN == -1) {
    for(audioN = 0; audioN < 8; audioN++) {
      if((vm->state).pgc->audio_control[audioN] & (1<<15)) {
        if ((streamN = vm_get_audio_stream(vm, audioN)) >= 0)
          break;
      }
    }
  }

  return streamN;
}

int vm_get_subp_active_stream(vm_t *vm, int mode) {
  int subpN;
  int streamN;
  subpN = (vm->state).SPST_REG & ~0x40;
  streamN = vm_get_subp_stream(vm, subpN, mode);
  
  /* If no such stream, then select the first one that exists. */
  if(streamN == -1) {
    for(subpN = 0; subpN < 32; subpN++) {
      if((vm->state).pgc->subp_control[subpN] & (1<<31)) {
        if ((streamN = vm_get_subp_stream(vm, subpN, mode)) >= 0)
          break;
      }
    }
  }

  if((vm->state).domain == VTS_DOMAIN && !((vm->state).SPST_REG & 0x40))
    /* Bit 7 set means hide, and only let Forced display show */
    return (streamN | 0x80);
  else
    return streamN;
}

void vm_get_angle_info(vm_t *vm, int *current, int *num_avail) {
  *num_avail = 1;
  *current = 1;
  
  if((vm->state).domain == VTS_DOMAIN) {
    title_info_t *title;
    /* TTN_REG does not allways point to the correct title.. */
    if((vm->state).TTN_REG > vm->vmgi->tt_srpt->nr_of_srpts)
      return;
    title = &vm->vmgi->tt_srpt->title[(vm->state).TTN_REG - 1];
    if(title->title_set_nr != (vm->state).vtsN || 
       title->vts_ttn != (vm->state).VTS_TTN_REG)
      return; 
    *num_avail = title->nr_of_angles;
    *current = (vm->state).AGL_REG;
  }
}

#if 0
/* currently unused */
void vm_get_audio_info(vm_t *vm, int *current, int *num_avail) {
  switch ((vm->state).domain) {
  case VTS_DOMAIN:
    *num_avail = vm->vtsi->vtsi_mat->nr_of_vts_audio_streams;
    *current = (vm->state).AST_REG;
    break;
  case VTSM_DOMAIN:
    *num_avail = vm->vtsi->vtsi_mat->nr_of_vtsm_audio_streams; /*  1 */
    *current = 1;
    break;
  case VMGM_DOMAIN:
  case FP_DOMAIN:
    *num_avail = vm->vmgi->vmgi_mat->nr_of_vmgm_audio_streams; /*  1 */
    *current = 1;
    break;
  }
}

/* currently unused */
void vm_get_subp_info(vm_t *vm, int *current, int *num_avail) {
  switch ((vm->state).domain) {
  case VTS_DOMAIN:
    *num_avail = vm->vtsi->vtsi_mat->nr_of_vts_subp_streams;
    *current = (vm->state).SPST_REG;
    break;
  case VTSM_DOMAIN:
    *num_avail = vm->vtsi->vtsi_mat->nr_of_vtsm_subp_streams; /*  1 */
    *current = 0x41;
    break;
  case VMGM_DOMAIN:
  case FP_DOMAIN:
    *num_avail = vm->vmgi->vmgi_mat->nr_of_vmgm_subp_streams; /*  1 */
    *current = 0x41;
    break;
  }
}

/* currently unused */
void vm_get_video_res(vm_t *vm, int *width, int *height) {
  video_attr_t attr = vm_get_video_attr(vm);
  
  if(attr.video_format != 0) 
    *height = 576;
  else
    *height = 480;
  switch(attr.picture_size) {
  case 0:
    *width = 720;
    break;
  case 1:
    *width = 704;
    break;
  case 2:
    *width = 352;
    break;
  case 3:
    *width = 352;
    *height /= 2;
    break;
  }
}
#endif

int vm_get_video_aspect(vm_t *vm) {
  int aspect = vm_get_video_attr(vm).display_aspect_ratio;
  
  assert(aspect == 0 || aspect == 3);
  (vm->state).registers.SPRM[14] &= ~(0x3 << 10);
  (vm->state).registers.SPRM[14] |= aspect << 10;
  
  return aspect;
}

int vm_get_video_scale_permission(vm_t *vm) {
  return vm_get_video_attr(vm).permitted_df;
}

video_attr_t vm_get_video_attr(vm_t *vm) {
  switch ((vm->state).domain) {
  case VTS_DOMAIN:
    return vm->vtsi->vtsi_mat->vts_video_attr;
  case VTSM_DOMAIN:
    return vm->vtsi->vtsi_mat->vtsm_video_attr;
  case VMGM_DOMAIN:
  case FP_DOMAIN:
    return vm->vmgi->vmgi_mat->vmgm_video_attr;
  default:
    abort();
  }
}

audio_attr_t vm_get_audio_attr(vm_t *vm, int streamN) {
  switch ((vm->state).domain) {
  case VTS_DOMAIN:
    return vm->vtsi->vtsi_mat->vts_audio_attr[streamN];
  case VTSM_DOMAIN:
    return vm->vtsi->vtsi_mat->vtsm_audio_attr;
  case VMGM_DOMAIN:
  case FP_DOMAIN:
    return vm->vmgi->vmgi_mat->vmgm_audio_attr;
  default:
    abort();
  }
}

subp_attr_t vm_get_subp_attr(vm_t *vm, int streamN) {
  switch ((vm->state).domain) {
  case VTS_DOMAIN:
    return vm->vtsi->vtsi_mat->vts_subp_attr[streamN];
  case VTSM_DOMAIN:
    return vm->vtsi->vtsi_mat->vtsm_subp_attr;
  case VMGM_DOMAIN:
  case FP_DOMAIN:
    return vm->vmgi->vmgi_mat->vmgm_subp_attr;
  default:
    abort();
  }
}


/* Playback control */

static link_t play_PGC(vm_t *vm) {
  link_t link_values;
  
  if((vm->state).domain != FP_DOMAIN) {
  TRACE(TRACE_DEBUG, "DVDNAV", "play_PGC: (vm->state).pgcN (%i)", 
	get_PGCN(vm));
  } else {
    TRACE(TRACE_DEBUG, "DVDNAV", "play_PGC: first_play_pgc");
  }

  /* This must be set before the pre-commands are executed because they
   * might contain a CallSS that will save resume state */

  /* FIXME: This may be only a temporary fix for something... */
  (vm->state).pgN = 1;
  (vm->state).cellN = 0;
  (vm->state).blockN = 0;

  /* eval -> updates the state and returns either 
     - some kind of jump (Jump(TT/SS/VTS_TTN/CallSS/link C/PG/PGC/PTTN)
     - just play video i.e first PG
       (This is what happens if you fall of the end of the pre_cmds)
     - or an error (are there more cases?) */
  if((vm->state).pgc->command_tbl && (vm->state).pgc->command_tbl->nr_of_pre) {
    if(vmEval_CMD((vm->state).pgc->command_tbl->pre_cmds, 
		  (vm->state).pgc->command_tbl->nr_of_pre, 
		  &(vm->state).registers, &link_values)) {
      /*  link_values contains the 'jump' return value */
      return link_values;
    } else {
      TRACE(TRACE_DEBUG, "DVDNAV", "PGC pre commands didn't do a Jump, Link or Call");
    }
  }
  return play_PG(vm);
}  

static link_t play_PGC_PG(vm_t *vm, int pgN) {    
  link_t link_values;
  
  if((vm->state).domain != FP_DOMAIN) {
    TRACE(TRACE_DEBUG, "DVDNAV", "play_PGC_PG: (vm->state).pgcN (%i)", 
	  get_PGCN(vm));
  } else {
    TRACE(TRACE_DEBUG, "DVDNAV", "play_PGC_PG: first_play_pgc");
  }

  /*  This must be set before the pre-commands are executed because they
   *  might contain a CallSS that will save resume state */

  /* FIXME: This may be only a temporary fix for something... */
  (vm->state).pgN = pgN;
  (vm->state).cellN = 0;
  (vm->state).blockN = 0;

  /* eval -> updates the state and returns either 
     - some kind of jump (Jump(TT/SS/VTS_TTN/CallSS/link C/PG/PGC/PTTN)
     - just play video i.e first PG
       (This is what happens if you fall of the end of the pre_cmds)
     - or an error (are there more cases?) */
  if((vm->state).pgc->command_tbl && (vm->state).pgc->command_tbl->nr_of_pre) {
    if(vmEval_CMD((vm->state).pgc->command_tbl->pre_cmds, 
		  (vm->state).pgc->command_tbl->nr_of_pre, 
		  &(vm->state).registers, &link_values)) {
      /*  link_values contains the 'jump' return value */
      return link_values;
    } else {
      TRACE(TRACE_DEBUG, "DVDNAV", "PGC pre commands didn't do a Jump, Link or Call");
    }
  }
  return play_PG(vm);
}  

static link_t play_PGC_post(vm_t *vm) {
  link_t link_values;
  
  TRACE(TRACE_DEBUG, "DVDNAV", "play_PGC_post:");
  
  /* eval -> updates the state and returns either 
     - some kind of jump (Jump(TT/SS/VTS_TTN/CallSS/link C/PG/PGC/PTTN)
     - just go to next PGC
       (This is what happens if you fall of the end of the post_cmds)
     - or an error (are there more cases?) */
  if((vm->state).pgc->command_tbl && (vm->state).pgc->command_tbl->nr_of_post &&
     vmEval_CMD((vm->state).pgc->command_tbl->post_cmds,
		(vm->state).pgc->command_tbl->nr_of_post, 
		&(vm->state).registers, &link_values)) {
    return link_values;
  }
  
  TRACE(TRACE_DEBUG, "DVDNAV", "** Fell of the end of the pgc, continuing in NextPGC");
  /* Should end up in the STOP_DOMAIN if next_pgc is 0. */
  if(!set_PGCN(vm, (vm->state).pgc->next_pgc_nr)) {
    link_values.command = Exit;
    return link_values;
  }
  return play_PGC(vm);
}

static link_t play_PG(vm_t *vm) {
  TRACE(TRACE_DEBUG, "DVDNAV", "play_PG: (vm->state).pgN (%i)", (vm->state).pgN);
  
  assert((vm->state).pgN > 0);
  if((vm->state).pgN > (vm->state).pgc->nr_of_programs) {
    TRACE(TRACE_DEBUG, "DVDNAV", 
	  "play_PG: (vm->state).pgN (%i) > pgc->nr_of_programs (%i)", 
	  (vm->state).pgN, (vm->state).pgc->nr_of_programs );
    assert((vm->state).pgN == (vm->state).pgc->nr_of_programs + 1); 
    return play_PGC_post(vm);
  }
  
  (vm->state).cellN = (vm->state).pgc->program_map[(vm->state).pgN - 1];
  
  return play_Cell(vm);
}

static link_t play_Cell(vm_t *vm) {
  static const link_t play_this = {PlayThis, /* Block in Cell */ 0, 0, 0};

  TRACE(TRACE_DEBUG, 
	"DVDNAV", "play_Cell: (vm->state).cellN (%i)", (vm->state).cellN);
  
  assert((vm->state).cellN > 0);
  if((vm->state).cellN > (vm->state).pgc->nr_of_cells) {
    TRACE(TRACE_DEBUG, "DVDNAV", 
	  "(vm->state).cellN (%i) > pgc->nr_of_cells (%i)", 
	  (vm->state).cellN, (vm->state).pgc->nr_of_cells );
    assert((vm->state).cellN == (vm->state).pgc->nr_of_cells + 1); 
    return play_PGC_post(vm);
  }
  
  /* Multi angle/Interleaved */
  switch((vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_mode) {
  case 0: /*  Normal */
    assert((vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_type == 0);
    break;
  case 1: /*  The first cell in the block */
    switch((vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_type) {
    case 0: /*  Not part of a block */
      assert(0);
      break;
    case 1: /*  Angle block */
      /* Loop and check each cell instead? So we don't get outside the block? */
      (vm->state).cellN += (vm->state).AGL_REG - 1;
#ifdef STRICT
      assert((vm->state).cellN <= (vm->state).pgc->nr_of_cells);
      assert((vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_mode != 0);
      assert((vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_type == 1);
#else
      if (!((vm->state).cellN <= (vm->state).pgc->nr_of_cells) ||
          !((vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_mode != 0) ||
	  !((vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_type == 1)) {
	TRACE(TRACE_ERROR, "DVDNAV", "Invalid angle block");
	(vm->state).cellN -= (vm->state).AGL_REG - 1;
      }
#endif
      break;
    case 2: /*  ?? */
    case 3: /*  ?? */
    default:
      TRACE(TRACE_ERROR, "DVDNAV", "Invalid? Cell block_mode (%d), block_type (%d)",
	      (vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_mode,
	      (vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_type);
      assert(0);
    }
    break;
  case 2: /*  Cell in the block */
  case 3: /*  Last cell in the block */
  /* These might perhaps happen for RSM or LinkC commands? */
  default:
    TRACE(TRACE_ERROR, "DVDNAV", "Cell is in block but did not enter at first cell!");
  }
  
  /* Updates (vm->state).pgN and PTTN_REG */
  if(!set_PGN(vm)) {
    /* Should not happen */
    assert(0);
    return play_PGC_post(vm);
  }
  (vm->state).cell_restart++;
  (vm->state).blockN = 0;
  TRACE(TRACE_DEBUG, "DVDNAV", "Cell should restart here");
  return play_this;
}

static link_t play_Cell_post(vm_t *vm) {
  cell_playback_t *cell;
  
  TRACE(TRACE_DEBUG, "DVDNAV",
	"play_Cell_post: (vm->state).cellN (%i)", (vm->state).cellN);
  
  cell = &(vm->state).pgc->cell_playback[(vm->state).cellN - 1];
  
  /* Still time is already taken care of before we get called. */
  
  /* Deal with a Cell command, if any */
  if(cell->cell_cmd_nr != 0) {
    link_t link_values;
    
/*  These asserts are now not needed.
 *  Some DVDs have no cell commands listed in the PGC,
 *  but the Cell itself points to a cell command that does not exist.
 *  For this situation, just ignore the cell command and continue.
 *
 *  assert((vm->state).pgc->command_tbl != NULL);
 *  assert((vm->state).pgc->command_tbl->nr_of_cell >= cell->cell_cmd_nr);
 */

    if ((vm->state).pgc->command_tbl != NULL &&
        (vm->state).pgc->command_tbl->nr_of_cell >= cell->cell_cmd_nr) {
      TRACE(TRACE_DEBUG, "DVDNAV", "Cell command present, executing");
      if(vmEval_CMD(&(vm->state).pgc->command_tbl->cell_cmds[cell->cell_cmd_nr - 1], 1,
		    &(vm->state).registers, &link_values)) {
        return link_values;
      } else {
        TRACE(TRACE_DEBUG, "DVDNAV", 
	      "Cell command didn't do a Jump, Link or Call");
      }
    } else {
      TRACE(TRACE_DEBUG, "DVDNAV", "Invalid Cell command");
    }
  }
  
  /* Where to continue after playing the cell... */
  /* Multi angle/Interleaved */
  switch((vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_mode) {
  case 0: /*  Normal */
    assert((vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_type == 0);
    (vm->state).cellN++;
    break;
  case 1: /*  The first cell in the block */
  case 2: /*  A cell in the block */
  case 3: /*  The last cell in the block */
  default:
    switch((vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_type) {
    case 0: /*  Not part of a block */
      assert(0);
      break;
    case 1: /*  Angle block */
      /* Skip the 'other' angles */
      (vm->state).cellN++;
      while((vm->state).cellN <= (vm->state).pgc->nr_of_cells &&
	    (vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_mode >= 2) {
	(vm->state).cellN++;
      }
      break;
    case 2: /*  ?? */
    case 3: /*  ?? */
    default:
      TRACE(TRACE_ERROR, "DVDNAV", "Invalid? Cell block_mode (%d), block_type (%d)",
	      (vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_mode,
	      (vm->state).pgc->cell_playback[(vm->state).cellN - 1].block_type);
      assert(0);
    }
    break;
  }
  
  /* Figure out the correct pgN for the new cell */ 
  if(!set_PGN(vm)) {
    TRACE(TRACE_DEBUG, "DVDNAV", "last cell in this PGC");
    return play_PGC_post(vm);
  }
  return play_Cell(vm);
}


/* link processing */

static int process_command(vm_t *vm, link_t link_values) {
  
  while(link_values.command != PlayThis) {
#if 0    
    TRACE(TRACE_DEBUG, "DVDNAV", "Before printout starts:");
    vm_print_link(link_values);
#endif
    TRACE(TRACE_DEBUG, "DVDNAV",
	  "Link values %i %i %i %i", link_values.command, 
	  link_values.data1, link_values.data2, link_values.data3);
    vm_print_current_domain_state(vm);
    TRACE(TRACE_DEBUG, "DVDNAV", "Before printout ends.");
    
    switch(link_values.command) {
    case LinkNoLink:
      /* BUTTON number:data1 */
      if(link_values.data1 != 0)
	(vm->state).HL_BTNN_REG = link_values.data1 << 10;
      return 0;  /* no actual jump */

    case LinkTopC:
      /* Restart playing from the beginning of the current Cell. */
      /* BUTTON number:data1 */
      if(link_values.data1 != 0)
	(vm->state).HL_BTNN_REG = link_values.data1 << 10;
      link_values = play_Cell(vm);
      break;
    case LinkNextC:
      /* Link to Next Cell */
      /* BUTTON number:data1 */
      if(link_values.data1 != 0)
	(vm->state).HL_BTNN_REG = link_values.data1 << 10;
      (vm->state).cellN += 1;
      link_values = play_Cell(vm);
      break;
    case LinkPrevC:
      /* Link to Previous Cell */
      /* BUTTON number:data1 */
      if(link_values.data1 != 0)
	(vm->state).HL_BTNN_REG = link_values.data1 << 10;
      assert((vm->state).cellN > 1);
      (vm->state).cellN -= 1;
      link_values = play_Cell(vm);
      break;
      
    case LinkTopPG:
      /* Link to Top of current Program */
      /* BUTTON number:data1 */
      if(link_values.data1 != 0)
	(vm->state).HL_BTNN_REG = link_values.data1 << 10;
      link_values = play_PG(vm);
      break;
    case LinkNextPG:
      /* Link to Next Program */
      /* BUTTON number:data1 */
      if(link_values.data1 != 0)
	(vm->state).HL_BTNN_REG = link_values.data1 << 10;
      (vm->state).pgN += 1;
      link_values = play_PG(vm);
      break;
    case LinkPrevPG:
      /* Link to Previous Program */
      /* BUTTON number:data1 */
      if(link_values.data1 != 0)
	(vm->state).HL_BTNN_REG = link_values.data1 << 10;
      assert((vm->state).pgN > 1);
      (vm->state).pgN -= 1;
      link_values = play_PG(vm);
      break;

    case LinkTopPGC:
      /* Restart playing from beginning of current Program Chain */
      /* BUTTON number:data1 */
      if(link_values.data1 != 0)
	(vm->state).HL_BTNN_REG = link_values.data1 << 10;
      link_values = play_PGC(vm);
      break;
    case LinkNextPGC:
      /* Link to Next Program Chain */
      /* BUTTON number:data1 */
      if(link_values.data1 != 0)
	(vm->state).HL_BTNN_REG = link_values.data1 << 10;
      assert((vm->state).pgc->next_pgc_nr != 0);
      if(set_PGCN(vm, (vm->state).pgc->next_pgc_nr))
	link_values = play_PGC(vm);
      else
	link_values.command = Exit;
      break;
    case LinkPrevPGC:
      /* Link to Previous Program Chain */
      /* BUTTON number:data1 */
      if(link_values.data1 != 0)
	(vm->state).HL_BTNN_REG = link_values.data1 << 10;
      assert((vm->state).pgc->prev_pgc_nr != 0);
      if(set_PGCN(vm, (vm->state).pgc->prev_pgc_nr))
	link_values = play_PGC(vm);
      else
	link_values.command = Exit;
      break;
    case LinkGoUpPGC:
      /* Link to GoUp Program Chain */
      /* BUTTON number:data1 */
      if(link_values.data1 != 0)
	(vm->state).HL_BTNN_REG = link_values.data1 << 10;
      assert((vm->state).pgc->goup_pgc_nr != 0);
      if(set_PGCN(vm, (vm->state).pgc->goup_pgc_nr))
	link_values = play_PGC(vm);
      else
	link_values.command = Exit;
      break;
    case LinkTailPGC:
      /* Link to Tail of Program Chain */
      /* BUTTON number:data1 */
      if(link_values.data1 != 0)
	(vm->state).HL_BTNN_REG = link_values.data1 << 10;
      link_values = play_PGC_post(vm);
    break;

    case LinkRSM:
      {
	/* Link to Resume point */
	int i;
	
	/* Check and see if there is any rsm info!! */
	if (!(vm->state).rsm_vtsN) {
	  TRACE(TRACE_ERROR, "DVDNAV", "trying to resume without any resume info set");
	  link_values.command = Exit;
	  break;
	}
	
	(vm->state).domain = VTS_DOMAIN;
	if (!ifoOpenNewVTSI(vm, vm->dvd, (vm->state).rsm_vtsN))
	  assert(0);
	set_PGCN(vm, (vm->state).rsm_pgcN);
	
	/* These should never be set in SystemSpace and/or MenuSpace */ 
	/* (vm->state).TTN_REG = rsm_tt; ?? */
	/* (vm->state).TT_PGCN_REG = (vm->state).rsm_pgcN; ?? */
	for(i = 0; i < 5; i++) {
	  (vm->state).registers.SPRM[4 + i] = (vm->state).rsm_regs[i];
	}
	
	if(link_values.data1 != 0)
	  (vm->state).HL_BTNN_REG = link_values.data1 << 10;
	
	if((vm->state).rsm_cellN == 0) {
	  assert((vm->state).cellN); /*  Checking if this ever happens */
	  (vm->state).pgN = 1;
	  link_values = play_PG(vm);
	} else { 
	  /* (vm->state).pgN = ?? this gets the right value in set_PGN() below */
	  (vm->state).cellN = (vm->state).rsm_cellN;
	  link_values.command = PlayThis;
	  link_values.data1 = (vm->state).rsm_blockN & 0xffff;
	  link_values.data2 = (vm->state).rsm_blockN >> 16;
	  if(!set_PGN(vm)) {
	    /* Were at the end of the PGC, should not happen for a RSM */
	    assert(0);
	    link_values.command = LinkTailPGC;
	    link_values.data1 = 0;  /* No button */
	  }
	}
      }
      break;
    case LinkPGCN:
      /* Link to Program Chain Number:data1 */
      if(!set_PGCN(vm, link_values.data1))
	assert(0);
      link_values = play_PGC(vm);
      break;
    case LinkPTTN:
      /* Link to Part of current Title Number:data1 */
      /* BUTTON number:data2 */
      /* PGC Pre-Commands are not executed */
      assert((vm->state).domain == VTS_DOMAIN);
      if(link_values.data2 != 0)
	(vm->state).HL_BTNN_REG = link_values.data2 << 10;
      if(!set_VTS_PTT(vm, (vm->state).vtsN, (vm->state).VTS_TTN_REG, link_values.data1))
	assert(0);
      link_values = play_PG(vm);
      break;
    case LinkPGN:
      /* Link to Program Number:data1 */
      /* BUTTON number:data2 */
      if(link_values.data2 != 0)
	(vm->state).HL_BTNN_REG = link_values.data2 << 10;
      /* Update any other state, PTTN perhaps? */
      (vm->state).pgN = link_values.data1;
      link_values = play_PG(vm);
      break;
    case LinkCN:
      /* Link to Cell Number:data1 */
      /* BUTTON number:data2 */
      if(link_values.data2 != 0)
	(vm->state).HL_BTNN_REG = link_values.data2 << 10;
      /* Update any other state, pgN, PTTN perhaps? */
      (vm->state).cellN = link_values.data1;
      link_values = play_Cell(vm);
      break;
      
    case Exit:
      vm->stopped = 1;
      return 0;
      
    case JumpTT:
      /* Jump to VTS Title Domain */
      /* Only allowed from the First Play domain(PGC) */
      /* or the Video Manager domain (VMG) */
      /* Stop SPRM9 Timer */
      /* Set SPRM1 and SPRM2 */
      assert((vm->state).domain == VMGM_DOMAIN || (vm->state).domain == FP_DOMAIN); /* ?? */
      if(set_TT(vm, link_values.data1))
        link_values = play_PGC(vm);
      else
	link_values.command = Exit;
      break;
    case JumpVTS_TT:
      /* Jump to Title:data1 in same VTS Title Domain */
      /* Only allowed from the VTS Menu Domain(VTSM) */
      /* or the Video Title Set Domain(VTS) */
      /* Stop SPRM9 Timer */
      /* Set SPRM1 and SPRM2 */
      assert((vm->state).domain == VTSM_DOMAIN || (vm->state).domain == VTS_DOMAIN); /* ?? */
      if(!set_VTS_TT(vm, (vm->state).vtsN, link_values.data1))
	assert(0);
      link_values = play_PGC(vm);
      break;
    case JumpVTS_PTT:
      /* Jump to Part:data2 of Title:data1 in same VTS Title Domain */
      /* Only allowed from the VTS Menu Domain(VTSM) */
      /* or the Video Title Set Domain(VTS) */
      /* Stop SPRM9 Timer */
      /* Set SPRM1 and SPRM2 */
      assert((vm->state).domain == VTSM_DOMAIN || (vm->state).domain == VTS_DOMAIN); /* ?? */
      if(!set_VTS_PTT(vm, (vm->state).vtsN, link_values.data1, link_values.data2))
	assert(0);
      link_values = play_PGC_PG(vm, (vm->state).pgN);
      break;
      
    case JumpSS_FP:
      /* Jump to First Play Domain */
      /* Only allowed from the VTS Menu Domain(VTSM) */
      /* or the Video Manager domain (VMG) */
      /* Stop SPRM9 Timer and any GPRM counters */
      assert((vm->state).domain == VMGM_DOMAIN || (vm->state).domain == VTSM_DOMAIN); /* ?? */
      if (!set_FP_PGC(vm))
	assert(0);
      link_values = play_PGC(vm);
      break;
    case JumpSS_VMGM_MENU:
      /* Jump to Video Manger domain - Title Menu:data1 or any PGC in VMG */
      /* Allowed from anywhere except the VTS Title domain */
      /* Stop SPRM9 Timer and any GPRM counters */
      assert((vm->state).domain != VTS_DOMAIN); /* ?? */
      (vm->state).domain = VMGM_DOMAIN;
      if(!set_MENU(vm, link_values.data1))
	assert(0);
      link_values = play_PGC(vm);
      break;
    case JumpSS_VTSM:
      /* Jump to a menu in Video Title domain, */
      /* or to a Menu is the current VTS */
      /* Stop SPRM9 Timer and any GPRM counters */
      /* ifoOpenNewVTSI:data1 */
      /* VTS_TTN_REG:data2 */
      /* get_MENU:data3 */ 
      if(link_values.data1 != 0) {
	if (link_values.data1 != (vm->state).vtsN) {
	  /* the normal case */
	  assert((vm->state).domain == VMGM_DOMAIN || (vm->state).domain == FP_DOMAIN); /* ?? */
	  (vm->state).domain = VTSM_DOMAIN;
	  if (!ifoOpenNewVTSI(vm, vm->dvd, link_values.data1))  /* Also sets (vm->state).vtsN */
	    assert(0);
	} else {
	  /* This happens on some discs like "Captain Scarlet & the Mysterons" or
	   * the German RC2 of "Anatomie" in VTSM. */
	  assert((vm->state).domain == VTSM_DOMAIN ||
	    (vm->state).domain == VMGM_DOMAIN || (vm->state).domain == FP_DOMAIN); /* ?? */
	  (vm->state).domain = VTSM_DOMAIN;
	}
      } else {
	/*  This happens on 'The Fifth Element' region 2. */
	assert((vm->state).domain == VTSM_DOMAIN);
      }
      /*  I don't know what title is supposed to be used for. */
      /*  Alien or Aliens has this != 1, I think. */
      /* assert(link_values.data2 == 1); */
      (vm->state).VTS_TTN_REG = link_values.data2;
      /* TTN_REG (SPRM4), VTS_TTN_REG (SPRM5), TT_PGCN_REG (SPRM6) are linked, */
      /* so if one changes, the others must change to match it. */
      (vm->state).TTN_REG     = get_TT(vm, (vm->state).vtsN, (vm->state).VTS_TTN_REG);
      if(!set_MENU(vm, link_values.data3))
	assert(0);
      link_values = play_PGC(vm);
      break;
    case JumpSS_VMGM_PGC:
      /* set_PGCN:data1 */
      /* Stop SPRM9 Timer and any GPRM counters */
      assert((vm->state).domain != VTS_DOMAIN); /* ?? */
      (vm->state).domain = VMGM_DOMAIN;
      if(!set_PGCN(vm, link_values.data1))
	assert(0);
      link_values = play_PGC(vm);
      break;
      
    case CallSS_FP:
      /* set_RSMinfo:data1 */
      assert((vm->state).domain == VTS_DOMAIN); /* ?? */
      /* Must be called before domain is changed */
      set_RSMinfo(vm, link_values.data1, /* We dont have block info */ 0);
      set_FP_PGC(vm);
      link_values = play_PGC(vm);
      break;
    case CallSS_VMGM_MENU:
      /* set_MENU:data1 */ 
      /* set_RSMinfo:data2 */
      assert((vm->state).domain == VTS_DOMAIN); /* ?? */
      /* Must be called before domain is changed */
      set_RSMinfo(vm, link_values.data2, /* We dont have block info */ 0);      
      (vm->state).domain = VMGM_DOMAIN;
      if(!set_MENU(vm, link_values.data1))
	assert(0);
      link_values = play_PGC(vm);
      break;
    case CallSS_VTSM:
      /* set_MENU:data1 */ 
      /* set_RSMinfo:data2 */
      assert((vm->state).domain == VTS_DOMAIN); /* ?? */
      /* Must be called before domain is changed */
      set_RSMinfo(vm, link_values.data2, /* We dont have block info */ 0);
      (vm->state).domain = VTSM_DOMAIN;
      if(!set_MENU(vm, link_values.data1))
	assert(0);
      link_values = play_PGC(vm);
      break;
    case CallSS_VMGM_PGC:
      /* set_PGC:data1 */
      /* set_RSMinfo:data2 */
      assert((vm->state).domain == VTS_DOMAIN); /* ?? */
      /* Must be called before domain is changed */
      set_RSMinfo(vm, link_values.data2, /* We dont have block info */ 0);
      (vm->state).domain = VMGM_DOMAIN;
      if(!set_PGCN(vm, link_values.data1))
	assert(0);
      link_values = play_PGC(vm);
      break;
    case PlayThis:
      /* Should never happen. */
      assert(0);
      break;
    }

    TRACE(TRACE_DEBUG, "DVDNAV", "After printout starts:");
    vm_print_current_domain_state(vm);
    TRACE(TRACE_DEBUG, "DVDNAV", "After printout ends.");
    
  }
  (vm->state).blockN = link_values.data1 | (link_values.data2 << 16);
  return 1;
}


/* Set functions */

static int set_TT(vm_t *vm, int tt) {  
  return set_PTT(vm, tt, 1);
}

static int set_PTT(vm_t *vm, int tt, int ptt) {
  assert(tt <= vm->vmgi->tt_srpt->nr_of_srpts);
  return set_VTS_PTT(vm, vm->vmgi->tt_srpt->title[tt - 1].title_set_nr,
		     vm->vmgi->tt_srpt->title[tt - 1].vts_ttn, ptt);
}

static int set_VTS_TT(vm_t *vm, int vtsN, int vts_ttn) {
  return set_VTS_PTT(vm, vtsN, vts_ttn, 1);
}

static int set_VTS_PTT(vm_t *vm, int vtsN, int vts_ttn, int part) {
  int pgcN, pgN, res;
  
  (vm->state).domain = VTS_DOMAIN;

  if (vtsN != (vm->state).vtsN)
    if (!ifoOpenNewVTSI(vm, vm->dvd, vtsN))  /* Also sets (vm->state).vtsN */
      return 0;
  
  if ((vts_ttn < 1) || (vts_ttn > vm->vtsi->vts_ptt_srpt->nr_of_srpts) ||
      (part < 1) || (part > vm->vtsi->vts_ptt_srpt->title[vts_ttn - 1].nr_of_ptts) ) {
    return 0;
  }
  
  pgcN = vm->vtsi->vts_ptt_srpt->title[vts_ttn - 1].ptt[part - 1].pgcn;
  pgN = vm->vtsi->vts_ptt_srpt->title[vts_ttn - 1].ptt[part - 1].pgn;
 
  (vm->state).TT_PGCN_REG = pgcN;
  (vm->state).PTTN_REG    = part;
  (vm->state).TTN_REG     = get_TT(vm, vtsN, vts_ttn);
  assert( (vm->state.TTN_REG) != 0 );
  (vm->state).VTS_TTN_REG = vts_ttn;
  (vm->state).vtsN        = vtsN;  /* Not sure about this one. We can get to it easily from TTN_REG */
  /* Any other registers? */
  
  res = set_PGCN(vm, pgcN);   /* This clobber's state.pgN (sets it to 1), but we don't want clobbering here. */
  (vm->state).pgN = pgN;
  return res;
}

static int set_FP_PGC(vm_t *vm) {  
  (vm->state).domain = FP_DOMAIN;
  if (!vm->vmgi->first_play_pgc) {
    return set_PGCN(vm, 1);
  }
  (vm->state).pgc = vm->vmgi->first_play_pgc;
  (vm->state).pgcN = vm->vmgi->vmgi_mat->first_play_pgc;
  return 1;
}


static int set_MENU(vm_t *vm, int menu) {
  assert((vm->state).domain == VMGM_DOMAIN || (vm->state).domain == VTSM_DOMAIN);
  return set_PGCN(vm, get_ID(vm, menu));
}

static int set_PGCN(vm_t *vm, int pgcN) {
  pgcit_t *pgcit;
  
  pgcit = get_PGCIT(vm);
  assert(pgcit != NULL);  /* ?? Make this return -1 instead */

  if(pgcN < 1 || pgcN > pgcit->nr_of_pgci_srp) {
    TRACE(TRACE_DEBUG, "DVDNAV", " ** No such pgcN = %d", pgcN);
    return 0;
  }
  
  (vm->state).pgc = pgcit->pgci_srp[pgcN - 1].pgc;
  (vm->state).pgcN = pgcN;
  (vm->state).pgN = 1;
 
  if((vm->state).domain == VTS_DOMAIN)
    (vm->state).TT_PGCN_REG = pgcN;

  return 1;
}

/* Figure out the correct pgN from the cell and update (vm->state). */ 
static int set_PGN(vm_t *vm) {
  int new_pgN = 0;
  
  while(new_pgN < (vm->state).pgc->nr_of_programs 
	&& (vm->state).cellN >= (vm->state).pgc->program_map[new_pgN])
    new_pgN++;
  
  if(new_pgN == (vm->state).pgc->nr_of_programs) /* We are at the last program */
    if((vm->state).cellN > (vm->state).pgc->nr_of_cells)
      return 0; /* We are past the last cell */
  
  (vm->state).pgN = new_pgN;
  
  if((vm->state).domain == VTS_DOMAIN) {
    playback_type_t *pb_ty;
    if((vm->state).TTN_REG > vm->vmgi->tt_srpt->nr_of_srpts)
      return 0; /* ?? */
    pb_ty = &vm->vmgi->tt_srpt->title[(vm->state).TTN_REG - 1].pb_ty;
    if(pb_ty->multi_or_random_pgc_title == /* One_Sequential_PGC_Title */ 0) {
      int dummy, part;
      vm_get_current_title_part(vm, &dummy, &part);
      (vm->state).PTTN_REG = part;
    } else {
      /* FIXME: Handle RANDOM or SHUFFLE titles. */
      TRACE(TRACE_ERROR, "DVDNAV", "RANDOM or SHUFFLE titles are NOT handled yet.");
    }
  }
  return 1;
}

/* Must be called before domain is changed (set_PGCN()) */
static void set_RSMinfo(vm_t *vm, int cellN, int blockN) {
  int i;
  
  if(cellN) {
    (vm->state).rsm_cellN = cellN;
    (vm->state).rsm_blockN = blockN;
  } else {
    (vm->state).rsm_cellN = (vm->state).cellN;
    (vm->state).rsm_blockN = blockN;
  }
  (vm->state).rsm_vtsN = (vm->state).vtsN;
  (vm->state).rsm_pgcN = get_PGCN(vm);
  
  /* assert((vm->state).rsm_pgcN == (vm->state).TT_PGCN_REG);  for VTS_DOMAIN */
  
  for(i = 0; i < 5; i++) {
    (vm->state).rsm_regs[i] = (vm->state).registers.SPRM[4 + i];
  }
}


/* Get functions */

/* Searches the TT tables, to find the current TT.
 * returns the current TT.
 * returns 0 if not found.
 */
static int get_TT(vm_t *vm, int vtsN, int vts_ttn) {
  int i;
  int tt=0;

  for(i = 1; i <= vm->vmgi->tt_srpt->nr_of_srpts; i++) {
    if( vm->vmgi->tt_srpt->title[i - 1].title_set_nr == vtsN && 
        vm->vmgi->tt_srpt->title[i - 1].vts_ttn == vts_ttn) {
      tt=i;
      break;
    }
  }
  return tt;
}

/* Search for entry_id match of the PGC Category in the current VTS PGCIT table.
 * Return pgcN based on entry_id match.
 */
static int get_ID(vm_t *vm, int id) {
  int pgcN, i;
  pgcit_t *pgcit;
  
  /* Relies on state to get the correct pgcit. */
  pgcit = get_PGCIT(vm);
  assert(pgcit != NULL);

  TRACE(TRACE_DEBUG, "DVDNAV", "** Searching for menu (0x%x) entry PGC", id);

  /* Force high bit set. */
  id |=0x80;

  /* Get menu/title */
  for(i = 0; i < pgcit->nr_of_pgci_srp; i++) {
    if( (pgcit->pgci_srp[i].entry_id) == id) {
      pgcN = i + 1;
      TRACE(TRACE_DEBUG, "DVDNAV", "Found menu.");
      return pgcN;
    }
  }
  TRACE(TRACE_DEBUG,
	"DVDNAV", "** No such id/menu (0x%02x) entry PGC", id & 0x7f);
  for(i = 0; i < pgcit->nr_of_pgci_srp; i++) {
    if ( (pgcit->pgci_srp[i].entry_id & 0x80) == 0x80) {
      TRACE(TRACE_DEBUG, "DVDNAV", "Available menus: 0x%x",
	    pgcit->pgci_srp[i].entry_id & 0x7f);
    }
  }
  return 0; /*  error */
}

/* FIXME: we have a pgcN member in the vm's state now, so this should be obsolete */
static int get_PGCN(vm_t *vm) {
  pgcit_t *pgcit;
  int pgcN = 1;

  pgcit = get_PGCIT(vm);
  
  if (pgcit) {
    while(pgcN <= pgcit->nr_of_pgci_srp) {
      if(pgcit->pgci_srp[pgcN - 1].pgc == (vm->state).pgc) {
	assert((vm->state).pgcN == pgcN);
	return pgcN;
      }
      pgcN++;
    }
  }
  TRACE(TRACE_ERROR, "DVDNAV", "get_PGCN failed. Was trying to find pgcN in domain %d", 
         (vm->state).domain);
  return 0; /*  error */
}

static pgcit_t* get_MENU_PGCIT(vm_t *vm, ifo_handle_t *h, uint16_t lang) {
  int i;
  
  if(h == NULL || h->pgci_ut == NULL) {
    TRACE(TRACE_ERROR, "DVDNAV", "*** pgci_ut handle is NULL ***");
    return NULL; /*  error? */
  }
  
  i = 0;
  while(i < h->pgci_ut->nr_of_lus
	&& h->pgci_ut->lu[i].lang_code != lang)
    i++;
  if(i == h->pgci_ut->nr_of_lus) {
    TRACE(TRACE_ERROR, "DVDNAV", "Language '%c%c' not found, using '%c%c' instead",
	    (char)(lang >> 8), (char)(lang & 0xff),
 	    (char)(h->pgci_ut->lu[0].lang_code >> 8),
	    (char)(h->pgci_ut->lu[0].lang_code & 0xff));
#if 0
    TRACE(TRACE_ERROR, "DVDNAV", "Menu Languages available: ");
    for(i = 0; i < h->pgci_ut->nr_of_lus; i++) {
      fprintf(MSG_OUT, "%c%c ",
 	    (char)(h->pgci_ut->lu[i].lang_code >> 8),
	    (char)(h->pgci_ut->lu[i].lang_code & 0xff));
    }
    fprintf(MSG_OUT, "");
#endif
    i = 0; /*  error? */
  }
  
  return h->pgci_ut->lu[i].pgcit;
}

/* Uses state to decide what to return */
static pgcit_t* get_PGCIT(vm_t *vm) {
  pgcit_t *pgcit = NULL;
  
  switch ((vm->state).domain) {
  case VTS_DOMAIN:
    if(!vm->vtsi) return NULL;
    pgcit = vm->vtsi->vts_pgcit;
    break;
  case VTSM_DOMAIN:
    if(!vm->vtsi) return NULL;
    pgcit = get_MENU_PGCIT(vm, vm->vtsi, (vm->state).registers.SPRM[0]);
    break;
  case VMGM_DOMAIN:
  case FP_DOMAIN:
    pgcit = get_MENU_PGCIT(vm, vm->vmgi, (vm->state).registers.SPRM[0]);
    break;
  default:
    abort();
  }
  
  return pgcit;
}

//return the ifo_handle_t describing required title, used to 
//identify chapters
ifo_handle_t *vm_get_title_ifo(vm_t *vm, uint32_t title)
{
  ifo_handle_t *ifo = NULL;
  uint8_t titleset_nr;
  if((title < 1) || (title > vm->vmgi->tt_srpt->nr_of_srpts))
    return NULL;
  titleset_nr = vm->vmgi->tt_srpt->title[title-1].title_set_nr;
  ifo = ifoOpen(vm->dvd, titleset_nr);
  return ifo;
}

void vm_ifo_close(ifo_handle_t *ifo)
{
  ifoClose(ifo);
}

/* Debug functions */

void vm_position_print(vm_t *vm, vm_position_t *position) {
  TRACE(TRACE_ERROR, "DVDNAV", "But=%x Spu=%x Aud=%x Ang=%x Hop=%x vts=%x dom=%x cell=%x cell_restart=%x cell_start=%x still=%x block=%x",
  position->button,
  position->spu_channel,
  position->audio_channel,
  position->angle_channel,
  position->hop_channel,
  position->vts,
  position->domain,
  position->cell,
  position->cell_restart,
  position->cell_start,
  position->still,
  position->block);
}

