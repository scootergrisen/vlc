/*****************************************************************************
 * familiar.c : familiar plugin for vlc
 *****************************************************************************
 * Copyright (C) 2002 VideoLAN
 * $Id: familiar.c,v 1.8.2.2 2002/09/30 22:01:43 jpsaman Exp $
 *
 * Authors: Jean-Paul Saman <jpsaman@wxs.nl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <stdlib.h>                                      /* malloc(), free() */
#include <errno.h>                                                 /* ENOMEM */
#include <string.h>                                            /* strerror() */
#include <stdio.h>

#include <videolan/vlc.h>

#include <gtk/gtk.h>

#include "stream_control.h"
#include "input_ext-intf.h"

#include "interface.h"
#include "intf_playlist.h"

#include "video.h"
#include "video_output.h"

#include "familiar_callbacks.h"
#include "familiar_interface.h"
#include "familiar_support.h"
#include "familiar.h"

/*****************************************************************************
 * Local variables (mutex-protected).
 *****************************************************************************/
static void ** pp_global_data = NULL;

/*****************************************************************************
 * g_atexit: kludge to avoid the Gtk+ thread to segfault at exit
 *****************************************************************************
 * gtk_init() makes several calls to g_atexit() which calls atexit() to
 * register tidying callbacks to be called at program exit. Since the Gtk+
 * plugin is likely to be unloaded at program exit, we have to export this
 * symbol to intercept the g_atexit() calls. Talk about crude hack.
 *****************************************************************************/
void g_atexit( GVoidFunc func )
{
    intf_thread_t *p_intf = p_main->p_intf;
    int i_dummy;

    if( pp_global_data == NULL )
    {
        atexit( func );
        return;
    }

    p_intf = (intf_thread_t *)*pp_global_data;
    if( p_intf == NULL )
    {
        return;
    }

    for( i_dummy = 0;
         i_dummy < MAX_ATEXIT && p_intf->p_sys->pf_callback[i_dummy] != NULL;
         i_dummy++ )
    {
        ;
    }

    if( i_dummy >= MAX_ATEXIT - 1 )
    {
        intf_ErrMsg( "too many atexit() callbacks to register" );
        return;
    }

    p_intf->p_sys->pf_callback[i_dummy]     = func;
    p_intf->p_sys->pf_callback[i_dummy + 1] = NULL;
}

/*****************************************************************************
 * Local prototypes.
 *****************************************************************************/
static void intf_getfunctions ( function_list_t * p_function_list );
static int  Open         ( intf_thread_t *p_intf );
static void Close        ( intf_thread_t *p_intf );
static void Run          ( intf_thread_t * );

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
MODULE_CONFIG_START
ADD_CATEGORY_HINT( N_("Miscellaneous"), NULL )
MODULE_CONFIG_STOP

MODULE_INIT_START
    SET_DESCRIPTION( _("Familiar Linux Gtk+ interface module") )
#ifndef WIN32
    if( getenv( "DISPLAY" ) == NULL )
    {
        ADD_CAPABILITY( INTF, 10 )
    }
    else
#endif
    {
        ADD_CAPABILITY( INTF, 70 )
    }
MODULE_INIT_STOP

MODULE_ACTIVATE_START
    intf_getfunctions( &p_module->p_functions->intf );
MODULE_ACTIVATE_STOP

MODULE_DEACTIVATE_START
MODULE_DEACTIVATE_STOP

/*****************************************************************************
 * Functions exported as capabilities. They are declared as static so that
 * we don't pollute the namespace too much.
 *****************************************************************************/
static void intf_getfunctions( function_list_t * p_function_list )
{
    p_function_list->functions.intf.pf_open  = Open;
    p_function_list->functions.intf.pf_close = Close;
    p_function_list->functions.intf.pf_run   = Run;
}

/*****************************************************************************
 * Open: initialize and create window
 *****************************************************************************/
static int Open( intf_thread_t *p_intf )
{   
    /* Allocate instance and initialize some members */
    p_intf->p_sys = malloc( sizeof( intf_sys_t ) );
    if( p_intf->p_sys == NULL )
    {
        intf_ErrMsg( "out of memory" );
        return (1);
    }

    /* Initialize Gtk+ thread */
    p_intf->p_sys->b_autoplayfile = 1;

    /* Initialize Gtk+ thread */
    p_intf->p_sys->p_input = NULL;
    p_intf->pf_run = Run;

    return (0);
}

/*****************************************************************************
 * Close: destroy interface window
 *****************************************************************************/
static void Close( intf_thread_t *p_intf )
{   
    if( p_intf->p_sys->p_input )
    {
//        vlc_object_release( p_intf->p_sys->p_input );
    }

    /* Destroy structure */
    free( p_intf->p_sys );
}

/*****************************************************************************
 * Run: Gtk+ thread
 *****************************************************************************
 * this part of the interface is in a separate thread so that we can call
 * gtk_main() from within it without annoying the rest of the program.
 *****************************************************************************/
static void Run( intf_thread_t *p_intf )
{
    /* gtk_init needs to know the command line. We don't care, so we
     * give it an empty one */
    char  *p_args[] = { "" };
    char **pp_args  = p_args;
    int    i_args   = 1;
    int    i_dummy  = 0;

    /* Initialize Gtk+ */
    gtk_set_locale ();

    /* gtk_init will register stuff with g_atexit, so we need to take
     * the global lock if we want to be able to intercept the calls */
    gtk_init( &i_args, &pp_args );

    /* Create some useful widgets that will certainly be used */
// FIXME: magic path
    add_pixmap_directory("share");

    p_intf->p_sys->p_window = create_familiar();
    if (p_intf->p_sys->p_window == NULL)
    {
        intf_ErrMsg( "unable to create familiar interface" );
    }

    /* Set the title of the main window */
    gtk_window_set_title( GTK_WINDOW(p_intf->p_sys->p_window),
                          VOUT_TITLE " (Familiar Linux interface)");

    p_intf->p_sys->p_notebook = GTK_NOTEBOOK( gtk_object_get_data(
        GTK_OBJECT( p_intf->p_sys->p_window ), "notebook" ) );
//    gtk_widget_hide( GTK_WIDGET(p_intf->p_sys->p_notebook) );

    p_intf->p_sys->p_progess = GTK_PROGRESS_BAR( gtk_object_get_data(
        GTK_OBJECT( p_intf->p_sys->p_window ), "progress" ) );
    gtk_widget_hide( GTK_WIDGET(p_intf->p_sys->p_progess) );

    p_intf->p_sys->p_clist = GTK_CLIST( gtk_object_get_data(
        GTK_OBJECT( p_intf->p_sys->p_window ), "clistmedia" ) );
    gtk_clist_set_column_visibility (GTK_CLIST (p_intf->p_sys->p_clist), 2, FALSE);
    gtk_clist_set_column_visibility (GTK_CLIST (p_intf->p_sys->p_clist), 3, FALSE);
    gtk_clist_set_column_visibility (GTK_CLIST (p_intf->p_sys->p_clist), 4, FALSE);
    gtk_clist_column_titles_show (GTK_CLIST (p_intf->p_sys->p_clist));

    /* Store p_intf to keep an eye on it */
    gtk_object_set_data( GTK_OBJECT(p_intf->p_sys->p_window),
                         "p_intf", p_intf );
    /* Show the control window */
    gtk_widget_show( p_intf->p_sys->p_window );
    ReadDirectory(p_intf->p_sys->p_clist, ".");

    /* Sleep to avoid using all CPU - since some interfaces needs to access
     * keyboard events, a 100ms delay is a good compromise */
//  i_dummy = gtk_timeout_add( INTF_IDLE_SLEEP / 1000, GtkManage, p_intf );

    /* Enter Gtk mode */
    gtk_main();

    intf_ErrMsg( "@@@ Run exiting interface" );

    /* Remove the timeout */
    gtk_timeout_remove( i_dummy );
    gtk_object_destroy( GTK_OBJECT(p_intf->p_sys->p_window) );

    /* Launch stored callbacks */
    for( i_dummy = 0;
         i_dummy < MAX_ATEXIT && p_intf->p_sys->pf_callback[i_dummy] != NULL;
         i_dummy++ )
    {
        p_intf->p_sys->pf_callback[i_dummy]();
    }
}

