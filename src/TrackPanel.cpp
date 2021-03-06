/**********************************************************************

  Audacity: A Digital Audio Editor

  TrackPanel.cpp

  Dominic Mazzoni
  and lots of other contributors

  Implements TrackPanel and TrackInfo.

********************************************************************//*!

\todo
  Refactoring of the TrackPanel, possibly as described
  in \ref TrackPanelRefactor

*//*****************************************************************//*!

\file TrackPanel.cpp
\brief
  Implements TrackPanel and TrackInfo.

  TrackPanel.cpp is currently some of the worst code in Audacity.
  It's not really unreadable, there's just way too much stuff in this
  one file.  Rather than apply a quick fix, the long-term plan
  is to create a GUITrack class that knows how to draw itself
  and handle events.  Then this class just helps coordinate
  between tracks.

  Plans under discussion are described in \ref TrackPanelRefactor

*//********************************************************************/

// Documentation: Rather than have a lengthy \todo section, having
// a \todo a \file and a \page in EXACTLY that order gets Doxygen to
// put the following lengthy description of refactoring on a NEW page
// and link to it from the docs.

/*****************************************************************//**

\class TrackPanel
\brief
  The TrackPanel class coordinates updates and operations on the
  main part of the screen which contains multiple tracks.

  It uses many other classes, but in particular it uses the
  TrackInfo class to draw the controls area on the left of a track,
  and the TrackArtist class to draw the actual waveforms.

  Note that in some of the older code here, e.g., GetLabelWidth(),
  "Label" means the TrackInfo plus the vertical ruler.
  Confusing relative to LabelTrack labels.

  The TrackPanel manages multiple tracks and their TrackInfos.

  Note that with stereo tracks there will be one TrackInfo
  being used by two wavetracks.

*//*****************************************************************//**

\class TrackInfo
\brief
  The TrackInfo is shown to the side of a track
  It has the menus, pan and gain controls displayed in it.
  So "Info" is somewhat a misnomer. Should possibly be "TrackControls".

  TrackPanel and not TrackInfo takes care of the functionality for
  each of the buttons in that panel.

  In its current implementation TrackInfo is not derived from a
  wxWindow.  Following the original coding style, it has
  been coded as a 'flyweight' class, which is passed
  state as needed, except for the array of gains and pans.

  If we'd instead coded it as a wxWindow, we would have an instance
  of this class for each instance displayed.

*//**************************************************************//**

\class TrackPanelListener
\brief A now badly named class which is used to give access to a
subset of the TrackPanel methods from all over the place.

*//**************************************************************//**

\class TrackList
\brief A list of TrackListNode items.

*//**************************************************************//**

\class TrackListNode
\brief Used by TrackList, points to a Track.

*//**************************************************************//**

\class TrackPanel::AudacityTimer
\brief Timer class dedicated to infomring the TrackPanel that it
is time to refresh some aspect of the screen.

*//*****************************************************************//**

\page TrackPanelRefactor Track Panel Refactor
\brief Planned refactoring of TrackPanel.cpp

 - Move menus from current TrackPanel into TrackInfo.
 - Convert TrackInfo from 'flyweight' to heavyweight.
 - Split GuiStereoTrack and GuiWaveTrack out from TrackPanel.

  JKC: Incremental refactoring started April/2003

  Possibly aiming for Gui classes something like this - it's under
  discussion:

<pre>
   +----------------------------------------------------+
   |      AdornedRulerPanel                             |
   +----------------------------------------------------+
   +----------------------------------------------------+
   |+------------+ +-----------------------------------+|
   ||            | | (L)  GuiWaveTrack                 ||
   || TrackInfo  | +-----------------------------------+|
   ||            | +-----------------------------------+|
   ||            | | (R)  GuiWaveTrack                 ||
   |+------------+ +-----------------------------------+|
   +-------- GuiStereoTrack ----------------------------+
   +----------------------------------------------------+
   |+------------+ +-----------------------------------+|
   ||            | | (L)  GuiWaveTrack                 ||
   || TrackInfo  | +-----------------------------------+|
   ||            | +-----------------------------------+|
   ||            | | (R)  GuiWaveTrack                 ||
   |+------------+ +-----------------------------------+|
   +-------- GuiStereoTrack ----------------------------+
</pre>

  With the whole lot sitting in a TrackPanel which forwards
  events to the sub objects.

  The GuiStereoTrack class will do the special logic for
  Stereo channel grouping.

  The precise names of the classes are subject to revision.
  Have deliberately not created NEW files for the NEW classes
  such as AdornedRulerPanel and TrackInfo - yet.

*//*****************************************************************/

#include "Audacity.h"
#include "Experimental.h"
#include "TrackPanel.h"
#include "Project.h"
#include "TrackPanelCellIterator.h"
#include "TrackPanelMouseEvent.h"
#include "TrackPanelResizeHandle.h"
//#define DEBUG_DRAW_TIMING 1

#include "AColor.h"
#include "AllThemeResources.h"
#include "AudioIO.h"
#include "float_cast.h"

#include "Prefs.h"
#include "RefreshCode.h"
#include "TrackArtist.h"
#include "TrackPanelAx.h"
#include "WaveTrack.h"
#ifdef EXPERIMENTAL_MIDI_OUT
#include "NoteTrack.h"
#endif

#include "toolbars/ControlToolBar.h"
#include "toolbars/ToolsToolBar.h"

//This loads the appropriate set of cursors, depending on platform.
#include "../images/Cursors.h"

#include "widgets/ASlider.h"
#include "widgets/Ruler.h"
#include <algorithm>

wxDEFINE_EVENT(EVT_TRACK_PANEL_TIMER, wxCommandEvent);

/*

This is a diagram of TrackPanel's division of one (non-stereo) track rectangle.
Total height equals Track::GetHeight()'s value.  Total width is the wxWindow's width.
Each charater that is not . represents one pixel.

Inset space of this track, and top inset of the next track, are used to draw the focus highlight.

Top inset of the right channel of a stereo track, and bottom shadow line of the
left channel, are used for the channel separator.

"Margin" is a term used for inset plus border (top and left) or inset plus
shadow plus border (right and bottom).

TrackInfo::GetTrackInfoWidth() == GetVRulerOffset()
counts columns from the left edge up to and including controls, and is a constant.

GetVRulerWidth() is variable -- all tracks have the same ruler width at any time,
but that width may be adjusted when tracks change their vertical scales.

GetLabelWidth() counts columns up to and including the VRuler.
GetLeftOffset() is yet one more -- it counts the "one pixel" column.

FindCell() for label returns a rectangle that OMITS left, top, and bottom
margins

FindCell() for vruler returns a rectangle right of the label,
up to and including the One Pixel column, and OMITS top and bottom margins

FindCell() for track returns a rectangle with x == GetLeftOffset(), and OMITS
right top, and bottom margins

+--------------- ... ------ ... --------------------- ...       ... -------------+
| Top Inset                                                                      |
|                                                                                |
|  +------------ ... ------ ... --------------------- ...       ... ----------+  |
| L|+-Border---- ... ------ ... --------------------- ...       ... -Border-+ |R |
| e||+---------- ... -++--- ... -+++----------------- ...       ... -------+| |i |
| f|B|                ||         |||                                       |BS|g |
| t|o| Controls       || V       |O|  The good stuff                       |oh|h |
|  |r|                || R       |n|                                       |ra|t |
| I|d|                || u       |e|                                       |dd|  |
| n|e|                || l       | |                                       |eo|I |
| s|r|                || e       |P|                                       |rw|n |
| e|||                || r       |i|                                       ||||s |
| t|||                ||         |x|                                       ||||e |
|  |||                ||         |e|                                       ||||t |
|  |||                ||         |l|                                       ||||  |
|  |||                ||         |||                                       ||||  |

.  ...                ..         ...                                       ....  .
.  ...                ..         ...                                       ....  .
.  ...                ..         ...                                       ....  .

|  |||                ||         |||                                       ||||  |
|  ||+----------     -++--  ... -+++----------------- ...       ... -------+|||  |
|  |+-Border---- ... -----  ... --------------------- ...       ... -Border-+||  |
|  |  Shadow---- ... -----  ... --------------------- ...       ... --Shadow-+|  |
*/

// Is the distance between A and B less than D?
template < class A, class B, class DIST > bool within(A a, B b, DIST d)
{
   return (a > b - d) && (a < b + d);
}

BEGIN_EVENT_TABLE(TrackPanel, CellularPanel)
    EVT_MOUSE_EVENTS(TrackPanel::OnMouseEvent)
    EVT_KEY_DOWN(TrackPanel::OnKeyDown)

    EVT_PAINT(TrackPanel::OnPaint)

    EVT_TIMER(wxID_ANY, TrackPanel::OnTimer)
END_EVENT_TABLE()

/// Makes a cursor from an XPM, uses CursorId as a fallback.
/// TODO:  Move this function to some other source file for reuse elsewhere.
std::unique_ptr<wxCursor> MakeCursor( int WXUNUSED(CursorId), const char * const pXpm[36],  int HotX, int HotY )
{
#ifdef CURSORS_SIZE32
   const int HotAdjust =0;
#else
   const int HotAdjust =8;
#endif

   wxImage Image = wxImage(wxBitmap(pXpm).ConvertToImage());
   Image.SetMaskColour(255,0,0);
   Image.SetMask();// Enable mask.

   Image.SetOption( wxIMAGE_OPTION_CUR_HOTSPOT_X, HotX-HotAdjust );
   Image.SetOption( wxIMAGE_OPTION_CUR_HOTSPOT_Y, HotY-HotAdjust );
   return std::make_unique<wxCursor>( Image );
}



// Don't warn us about using 'this' in the base member initializer list.
#ifndef __WXGTK__ //Get rid if this pragma for gtk
#pragma warning( disable: 4355 )
#endif
TrackPanel::TrackPanel(wxWindow * parent, wxWindowID id,
                       const wxPoint & pos,
                       const wxSize & size,
                       const std::shared_ptr<TrackList> &tracks,
                       ViewInfo * viewInfo,
                       TrackPanelListener * listener,
                       AdornedRulerPanel * ruler)
   : CellularPanel(parent, id, pos, size, viewInfo,
                   wxWANTS_CHARS | wxNO_BORDER),
     mTrackInfo(this),
     mListener(listener),
     mTracks(tracks),
     mRuler(ruler),
     mTrackArtist(nullptr),
     mRefreshBacking(false),
     vrulerSize(36,0)
#ifndef __WXGTK__   //Get rid if this pragma for gtk
#pragma warning( default: 4355 )
#endif
{
   SetLayoutDirection(wxLayout_LeftToRight);
   SetLabel(_("Track Panel"));
   SetName(_("Track Panel"));
   SetBackgroundStyle(wxBG_STYLE_PAINT);

   {
      auto pAx = std::make_unique <TrackPanelAx>( this );
#if wxUSE_ACCESSIBILITY
      // wxWidgets owns the accessible object
      SetAccessible(mAx = pAx.release());
#else
      // wxWidgets does not own the object, but we need to retain it
      mAx = std::move(pAx);
#endif
   }

   mRedrawAfterStop = false;

   mTrackArtist = std::make_unique<TrackArtist>();

   mTrackArtist->SetMargins(1, kTopMargin, kRightMargin, kBottomMargin);

   mTimeCount = 0;
   mTimer.parent = this;
   // Timer is started after the window is visible
   GetProject()->Bind(wxEVT_IDLE, &TrackPanel::OnIdle, this);

   // Register for tracklist updates
   mTracks->Bind(EVT_TRACKLIST_RESIZING,
                    &TrackPanel::OnTrackListResizing,
                    this);
   mTracks->Bind(EVT_TRACKLIST_DELETION,
                    &TrackPanel::OnTrackListDeletion,
                    this);
   wxTheApp->Bind(EVT_AUDIOIO_PLAYBACK,
                     &TrackPanel::OnPlayback,
                     this);
}


TrackPanel::~TrackPanel()
{
   mTimer.Stop();

   // This can happen if a label is being edited and the user presses
   // ALT+F4 or Command+Q
   if (HasCapture())
      ReleaseMouse();
}

LWSlider *TrackPanel::GainSlider( const WaveTrack *wt )
{
   auto rect = FindTrackRect( wt, true );
   wxRect sliderRect;
   TrackInfo::GetGainRect( rect.GetTopLeft(), sliderRect );
   return TrackInfo::GainSlider(sliderRect, wt, false, this);
}

LWSlider *TrackPanel::PanSlider( const WaveTrack *wt )
{
   auto rect = FindTrackRect( wt, true );
   wxRect sliderRect;
   TrackInfo::GetPanRect( rect.GetTopLeft(), sliderRect );
   return TrackInfo::PanSlider(sliderRect, wt, false, this);
}

#ifdef EXPERIMENTAL_MIDI_OUT
LWSlider *TrackPanel::VelocitySlider( const NoteTrack *nt )
{
   auto rect = FindTrackRect( nt, true );
   wxRect sliderRect;
   TrackInfo::GetVelocityRect( rect.GetTopLeft(), sliderRect );
   return TrackInfo::VelocitySlider(sliderRect, nt, false, this);
}
#endif

wxString TrackPanel::gSoloPref;

void TrackPanel::UpdatePrefs()
{
   gPrefs->Read(wxT("/GUI/AutoScroll"), &mViewInfo->bUpdateTrackIndicator,
      true);
   gPrefs->Read(wxT("/GUI/Solo"), &gSoloPref, wxT("Simple"));

   mViewInfo->UpdatePrefs();

   if (mTrackArtist) {
      mTrackArtist->UpdatePrefs();
   }

   // All vertical rulers must be recalculated since the minimum and maximum
   // frequences may have been changed.
   UpdateVRulers();

   mTrackInfo.UpdatePrefs();

   Refresh();
}

void TrackPanel::ApplyUpdatedTheme()
{
   mTrackInfo.ReCreateSliders();
}


void TrackPanel::GetTracksUsableArea(int *width, int *height) const
{
   GetSize(width, height);
   if (width) {
      *width -= GetLeftOffset();
      *width -= kRightMargin;
      *width = std::max(0, *width);
   }
}

/// Gets the pointer to the AudacityProject that
/// goes with this track panel.
AudacityProject * TrackPanel::GetProject() const
{
   //JKC casting away constness here.
   //Do it in two stages in case 'this' is not a wxWindow.
   //when the compiler will flag the error.
   wxWindow const * const pConstWind = this;
   wxWindow * pWind=(wxWindow*)pConstWind;
#ifdef EXPERIMENTAL_NOTEBOOK
   pWind = pWind->GetParent(); //Page
   wxASSERT( pWind );
   pWind = pWind->GetParent(); //Notebook
   wxASSERT( pWind );
#endif
   pWind = pWind->GetParent(); //MainPanel
   wxASSERT( pWind );
   pWind = pWind->GetParent(); //Project
   wxASSERT( pWind );
   return (AudacityProject*)pWind;
}

void TrackPanel::OnIdle(wxIdleEvent& event)
{
   // The window must be ready when the timer fires (#1401)
   if (IsShownOnScreen())
   {
      mTimer.Start(kTimerInterval, FALSE);

      // Timer is started, we don't need the event anymore
      GetProject()->Unbind(wxEVT_IDLE, &TrackPanel::OnIdle, this);
   }
   else
   {
      // Get another idle event, wx only guarantees we get one
      // event after "some other normal events occur"
      event.RequestMore();
   }
}

/// AS: This gets called on our wx timer events.
void TrackPanel::OnTimer(wxTimerEvent& )
{
#ifdef __WXMAC__
   // Unfortunate part of fix for bug 1431
   // Without this, the toolbars hide only every other time that you press
   // the yellow title bar button.  For some reason, not every press sends
   // us a deactivate event for the application.
   {
      auto project = GetProject();
      if (project->IsIconized())
         project->MacShowUndockedToolbars(false);
   }
#endif

   mTimeCount++;

   AudacityProject *const p = GetProject();

   // Check whether we were playing or recording, but the stream has stopped.
   if (p->GetAudioIOToken()>0 && !IsAudioActive())
   {
      //the stream may have been started up after this one finished (by some other project)
      //in that case reset the buttons don't stop the stream
      p->GetControlToolBar()->StopPlaying(!gAudioIO->IsStreamActive());
   }

   // Next, check to see if we were playing or recording
   // audio, but now Audio I/O is completely finished.
   if (p->GetAudioIOToken()>0 &&
         !gAudioIO->IsAudioTokenActive(p->GetAudioIOToken()))
   {
      p->FixScrollbars();
      p->SetAudioIOToken(0);
      p->RedrawProject();

      mRedrawAfterStop = false;

      //ANSWER-ME: Was DisplaySelection added to solve a repaint problem?
      DisplaySelection();
   }
   if (mLastDrawnSelectedRegion != mViewInfo->selectedRegion) {
      UpdateSelectionDisplay();
   }

   // Notify listeners for timer ticks
   {
      wxCommandEvent e(EVT_TRACK_PANEL_TIMER);
      p->GetEventHandler()->ProcessEvent(e);
   }

   DrawOverlays(false);
   mRuler->DrawOverlays(false);

   if(IsAudioActive() && gAudioIO->GetNumCaptureChannels()) {

      // Periodically update the display while recording

      if (!mRedrawAfterStop) {
         mRedrawAfterStop = true;
         MakeParentRedrawScrollbars();
         mListener->TP_ScrollUpDown( 99999999 );
         Refresh( false );
      }
      else {
         if ((mTimeCount % 5) == 0) {
            // Must tell OnPaint() to recreate the backing bitmap
            // since we've not done a full refresh.
            mRefreshBacking = true;
            Refresh( false );
         }
      }
   }
   if(mTimeCount > 1000)
      mTimeCount = 0;
}

double TrackPanel::GetScreenEndTime() const
{
   int width;
   GetTracksUsableArea(&width, NULL);
   return mViewInfo->PositionToTime(width, 0, true);
}

/// AS: OnPaint( ) is called during the normal course of
///  completing a repaint operation.
void TrackPanel::OnPaint(wxPaintEvent & /* event */)
{
   mLastDrawnSelectedRegion = mViewInfo->selectedRegion;

#if DEBUG_DRAW_TIMING
   wxStopWatch sw;
#endif

   {
      wxPaintDC dc(this);

      // Retrieve the damage rectangle
      wxRect box = GetUpdateRegion().GetBox();

      // Recreate the backing bitmap if we have a full refresh
      // (See TrackPanel::Refresh())
      if (mRefreshBacking || (box == GetRect()))
      {
         // Reset (should a mutex be used???)
         mRefreshBacking = false;

         // Redraw the backing bitmap
         DrawTracks(&GetBackingDCForRepaint());

         // Copy it to the display
         DisplayBitmap(dc);
      }
      else
      {
         // Copy full, possibly clipped, damage rectangle
         RepairBitmap(dc, box.x, box.y, box.width, box.height);
      }

      // Done with the clipped DC

      // Drawing now goes directly to the client area.
      // DrawOverlays() may need to draw outside the clipped region.
      // (Used to make a NEW, separate wxClientDC, but that risks flashing
      // problems on Mac.)
      dc.DestroyClippingRegion();
      DrawOverlays(true, &dc);
   }

#if DEBUG_DRAW_TIMING
   sw.Pause();
   wxLogDebug(wxT("Total: %ld milliseconds"), sw.Time());
   wxPrintf(wxT("Total: %ld milliseconds\n"), sw.Time());
#endif
}

void TrackPanel::MakeParentModifyState(bool bWantsAutoSave)
{
   mListener->TP_ModifyState(bWantsAutoSave);
}

void TrackPanel::MakeParentRedrawScrollbars()
{
   mListener->TP_RedrawScrollbars();
}

namespace {
   std::shared_ptr<Track> FindTrack(TrackPanelCell *pCell )
   {
      if (pCell)
         return static_cast<CommonTrackPanelCell*>( pCell )->FindTrack();
      return {};
   }
}

void TrackPanel::ProcessUIHandleResult
   (TrackPanelCell *pClickedCell, TrackPanelCell *pLatestCell,
    UIHandle::Result refreshResult)
{
   const auto panel = this;
   auto pLatestTrack = FindTrack( pLatestCell ).get();

   // This precaution checks that the track is not only nonnull, but also
   // really owned by the track list
   auto pClickedTrack = GetTracks()->Lock(
      std::weak_ptr<Track>{ FindTrack( pClickedCell ) }
   ).get();

   // TODO:  make a finer distinction between refreshing the track control area,
   // and the waveform area.  As it is, redraw both whenever you must redraw either.

   // Copy data from the underlying tracks to the pending tracks that are
   // really displayed
   panel->GetProject()->GetTracks()->UpdatePendingTracks();

   using namespace RefreshCode;

   if (refreshResult & DestroyedCell) {
      panel->UpdateViewIfNoTracks();
      // Beware stale pointer!
      if (pLatestTrack == pClickedTrack)
         pLatestTrack = NULL;
      pClickedTrack = NULL;
   }

   if (pClickedTrack && (refreshResult & RefreshCode::UpdateVRuler))
      panel->UpdateVRuler(pClickedTrack);

   if (refreshResult & RefreshCode::DrawOverlays) {
      panel->DrawOverlays(false);
      mRuler->DrawOverlays(false);
   }

   // Refresh all if told to do so, or if told to refresh a track that
   // is not known.
   const bool refreshAll =
      (    (refreshResult & RefreshAll)
       || ((refreshResult & RefreshCell) && !pClickedTrack)
       || ((refreshResult & RefreshLatestCell) && !pLatestTrack));

   if (refreshAll)
      panel->Refresh(false);
   else {
      if (refreshResult & RefreshCell)
         panel->RefreshTrack(pClickedTrack);
      if (refreshResult & RefreshLatestCell)
         panel->RefreshTrack(pLatestTrack);
   }

   if (refreshResult & FixScrollbars)
      panel->MakeParentRedrawScrollbars();

   if (refreshResult & Resize)
      panel->GetListener()->TP_HandleResize();

   // This flag is superfluous if you do full refresh,
   // because TrackPanel::Refresh() does this too
   if (refreshResult & UpdateSelection) {
      panel->DisplaySelection();

      {
         // Formerly in TrackPanel::UpdateSelectionDisplay():

         // Make sure the ruler follows suit.
         // mRuler->DrawSelection();

         // ... but that too is superfluous it does nothing but refresh
         // the ruler, while DisplaySelection calls TP_DisplaySelection which
         // also always refreshes the ruler.
      }
   }

   if ((refreshResult & RefreshCode::EnsureVisible) && pClickedTrack)
      panel->EnsureVisible(pClickedTrack);
}

void TrackPanel::HandlePageUpKey()
{
   mListener->TP_ScrollWindow(2 * mViewInfo->h - GetScreenEndTime());
}

void TrackPanel::HandlePageDownKey()
{
   mListener->TP_ScrollWindow(GetScreenEndTime());
}

bool TrackPanel::IsAudioActive()
{
   AudacityProject *p = GetProject();
   return p->IsAudioActive();
}

void TrackPanel::UpdateStatusMessage( const wxString &st )
{
   auto status = st;
   if (HasEscape())
   /* i18n-hint Esc is a key on the keyboard */
      status += wxT(" "), status += _("(Esc to cancel)");
   mListener->TP_DisplayStatusMessage(status);
}

bool TrackPanel::TakesFocus() const
{
   return true;
}

void TrackPanel::UpdateSelectionDisplay()
{
   // Full refresh since the label area may need to indicate
   // newly selected tracks.
   Refresh(false);

   // Make sure the ruler follows suit.
   mRuler->DrawSelection();

   // As well as the SelectionBar.
   DisplaySelection();
}

void TrackPanel::UpdateAccessibility()
{
   if (mAx)
      mAx->Updated();
}

// Counts tracks, counting stereo tracks as one track.
size_t TrackPanel::GetTrackCount() const
{
  return GetTracks()->Leaders().size();
}

// Counts selected tracks, counting stereo tracks as one track.
size_t TrackPanel::GetSelectedTrackCount() const
{
   return GetTracks()->SelectedLeaders().size();
}

void TrackPanel::MessageForScreenReader(const wxString& message)
{
   if (mAx)
      mAx->MessageForScreenReader(message);
}

void TrackPanel::UpdateViewIfNoTracks()
{
   if (mTracks->empty())
   {
      // BG: There are no more tracks on screen
      //BG: Set zoom to normal
      mViewInfo->SetZoom(ZoomInfo::GetDefaultZoom());

      //STM: Set selection to 0,0
      //PRL: and default the rest of the selection information
      mViewInfo->selectedRegion = SelectedRegion();

      // PRL:  Following causes the time ruler to align 0 with left edge.
      // Bug 972
      mViewInfo->h = 0;

      mListener->TP_RedrawScrollbars();
      mListener->TP_HandleResize();
      mListener->TP_DisplayStatusMessage(wxT("")); //STM: Clear message if all tracks are removed
   }
}

void TrackPanel::OnPlayback(wxCommandEvent &e)
{
   e.Skip();
   // Starting or stopping of play or record affects some cursors.
   // Start or stop is in progress now, not completed; so delay the cursor
   // change until next idle time.
   CallAfter( [this] { HandleCursorForPresentMouseState(); } );
}

// The tracks positions within the list have changed, so update the vertical
// ruler size for the track that triggered the event.
void TrackPanel::OnTrackListResizing(wxCommandEvent & e)
{
   auto t = static_cast<TrackListEvent&>(e).mpTrack.lock();
   // A deleted track can trigger the event.  In which case do nothing here.
   if( t )
      UpdateVRuler(t.get());
   e.Skip();
}

// Tracks have been removed from the list.
void TrackPanel::OnTrackListDeletion(wxCommandEvent & e)
{
   // copy shared_ptr for safety, as in HandleClick
   auto handle = Target();
   if (handle) {
      handle->OnProjectChange(GetProject());
   }

   // If the focused track disappeared but there are still other tracks,
   // this reassigns focus.
   GetFocusedTrack();

   UpdateVRulerSize();

   e.Skip();
}

struct TrackInfo::TCPLine {
   using DrawFunction = void (*)(
      TrackPanelDrawingContext &context,
      const wxRect &rect,
      const Track *maybeNULL
   );

   unsigned items; // a bitwise OR of values of the enum above
   int height;
   int extraSpace;
   DrawFunction drawFunction;
};

namespace {

#define RANGE(array) (array), (array) + sizeof(array)/sizeof(*(array))
using TCPLines = std::vector< TrackInfo::TCPLine >;

enum : unsigned {
   // The sequence is not significant, just keep bits distinct
   kItemBarButtons       = 1 << 0,
   kItemStatusInfo1      = 1 << 1,
   kItemMute             = 1 << 2,
   kItemSolo             = 1 << 3,
   kItemGain             = 1 << 4,
   kItemPan              = 1 << 5,
   kItemVelocity         = 1 << 6,
   kItemMidiControlsRect = 1 << 7,
   kItemMinimize         = 1 << 8,
   kItemSyncLock         = 1 << 9,
   kItemStatusInfo2      = 1 << 10,

   kHighestBottomItem = kItemMinimize,
};


#ifdef EXPERIMENTAL_DA

   #define TITLE_ITEMS \
      { kItemBarButtons, kTrackInfoBtnSize, 4, \
        &TrackInfo::CloseTitleDrawFunction },
   // DA: Has Mute and Solo on separate lines.
   #define MUTE_SOLO_ITEMS(extra) \
      { kItemMute, kTrackInfoBtnSize + 1, 1, \
        &TrackInfo::WideMuteDrawFunction }, \
      { kItemSolo, kTrackInfoBtnSize + 1, extra, \
        &TrackInfo::WideSoloDrawFunction },
   // DA: Does not have status information for a track.
   #define STATUS_ITEMS

#else

   #define TITLE_ITEMS \
      { kItemBarButtons, kTrackInfoBtnSize, 0, \
        &TrackInfo::CloseTitleDrawFunction },
   #define MUTE_SOLO_ITEMS(extra) \
      { kItemMute | kItemSolo, kTrackInfoBtnSize + 1, extra, \
        &TrackInfo::MuteAndSoloDrawFunction },
   #define STATUS_ITEMS \
      { kItemStatusInfo1, 12, 0, \
        &TrackInfo::Status1DrawFunction }, \
      { kItemStatusInfo2, 12, 0, \
        &TrackInfo::Status2DrawFunction },

#endif

#define COMMON_ITEMS \
   TITLE_ITEMS

const TrackInfo::TCPLine defaultCommonTrackTCPLines[] = {
   COMMON_ITEMS
};
TCPLines commonTrackTCPLines{ RANGE(defaultCommonTrackTCPLines) };

const TrackInfo::TCPLine defaultWaveTrackTCPLines[] = {
   COMMON_ITEMS
   MUTE_SOLO_ITEMS(2)
   { kItemGain, kTrackInfoSliderHeight, kTrackInfoSliderExtra,
     &TrackInfo::GainSliderDrawFunction },
   { kItemPan, kTrackInfoSliderHeight, kTrackInfoSliderExtra,
     &TrackInfo::PanSliderDrawFunction },
   STATUS_ITEMS
};
TCPLines waveTrackTCPLines{ RANGE(defaultWaveTrackTCPLines) };

const TrackInfo::TCPLine defaultNoteTrackTCPLines[] = {
   COMMON_ITEMS
#ifdef EXPERIMENTAL_MIDI_OUT
   MUTE_SOLO_ITEMS(0)
   { kItemMidiControlsRect, kMidiCellHeight * 4, 0,
     &TrackInfo::MidiControlsDrawFunction },
   { kItemVelocity, kTrackInfoSliderHeight, kTrackInfoSliderExtra,
     &TrackInfo::VelocitySliderDrawFunction },
#endif
};
TCPLines noteTrackTCPLines{ RANGE(defaultNoteTrackTCPLines) };

int totalTCPLines( const TCPLines &lines, bool omitLastExtra )
{
   int total = 0;
   int lastExtra = 0;
   for ( const auto line : lines ) {
      lastExtra = line.extraSpace;
      total += line.height + lastExtra;
   }
   if (omitLastExtra)
      total -= lastExtra;
   return total;
}

const TCPLines &getTCPLines( const Track &track )
{
   auto lines = track.TypeSwitch< TCPLines * >(
#ifdef USE_MIDI
      [](const NoteTrack*){
         return &noteTrackTCPLines;
      },
#endif
      [](const WaveTrack*){
         return &waveTrackTCPLines;
      },
      [](const Track*){
         return &commonTrackTCPLines;
      }
   );

   if (lines)
      return *lines;

   return commonTrackTCPLines;
}

// return y value and height
std::pair< int, int > CalcItemY( const TCPLines &lines, unsigned iItem )
{
   int y = 0;
   auto pLines = lines.begin();
   while ( pLines != lines.end() &&
           0 == (pLines->items & iItem) ) {
      y += pLines->height + pLines->extraSpace;
      ++pLines;
   }
   int height = 0;
   if ( pLines != lines.end() )
      height = pLines->height;
   return { y, height };
}

// Items for the bottom of the panel, listed bottom-upwards
// As also with the top items, the extra space is below the item
const TrackInfo::TCPLine defaultCommonTrackTCPBottomLines[] = {
   // The '0' avoids impinging on bottom line of TCP
   // Use -1 if you do want to do so.
   { kItemSyncLock | kItemMinimize, kTrackInfoBtnSize, 0,
     &TrackInfo::MinimizeSyncLockDrawFunction },
};
TCPLines commonTrackTCPBottomLines{ RANGE(defaultCommonTrackTCPBottomLines) };

// return y value and height
std::pair< int, int > CalcBottomItemY
   ( const TCPLines &lines, unsigned iItem, int height )
{
   int y = height;
   auto pLines = lines.begin();
   while ( pLines != lines.end() &&
           0 == (pLines->items & iItem) ) {
      y -= pLines->height + pLines->extraSpace;
      ++pLines;
   }
   if (pLines != lines.end())
      y -= (pLines->height + pLines->extraSpace );
   return { y, pLines->height };
}

}

unsigned TrackInfo::MinimumTrackHeight()
{
   unsigned height = 0;
   if (!commonTrackTCPLines.empty())
      height += commonTrackTCPLines.front().height;
   if (!commonTrackTCPBottomLines.empty())
      height += commonTrackTCPBottomLines.front().height;
   // + 1 prevents the top item from disappearing for want of enough space,
   // according to the rules in HideTopItem.
   return height + kTopMargin + kBottomMargin + 1;
}

bool TrackInfo::HideTopItem( const wxRect &rect, const wxRect &subRect,
                 int allowance ) {
   auto limit = CalcBottomItemY
   ( commonTrackTCPBottomLines, kHighestBottomItem, rect.height).first;
   // Return true if the rectangle is even touching the limit
   // without an overlap.  That was the behavior as of 2.1.3.
   return subRect.y + subRect.height - allowance >= rect.y + limit;
}

void TrackPanel::OnKeyDown(wxKeyEvent & event)
{
   switch (event.GetKeyCode())
   {
      // Allow PageUp and PageDown keys to
      //scroll the Track Panel left and right
   case WXK_PAGEUP:
      HandlePageUpKey();
      return;

   case WXK_PAGEDOWN:
      HandlePageDownKey();
      return;
      
   default:
      // fall through to base class handler
      event.Skip();
   }
}

void TrackPanel::OnMouseEvent(wxMouseEvent & event)
{
   if (event.LeftDown()) {
      // wxTimers seem to be a little unreliable, so this
      // "primes" it to make sure it keeps going for a while...

      // When this timer fires, we call TrackPanel::OnTimer and
      // possibly update the screen for offscreen scrolling.
      mTimer.Stop();
      mTimer.Start(kTimerInterval, FALSE);
   }


   if (event.ButtonUp()) {
      //EnsureVisible should be called after processing the up-click.
      this->CallAfter( [this, event]{
         const auto foundCell = FindCell(event.m_x, event.m_y);
         const auto t = FindTrack( foundCell.pCell.get() );
         if ( t )
            EnsureVisible(t.get());
      } );
   }

   // Must also fall through to base class handler
   event.Skip();
}

double TrackPanel::GetMostRecentXPos()
{
   return mViewInfo->PositionToTime(MostRecentXCoord(), GetLabelWidth());
}

void TrackPanel::RefreshTrack(Track *trk, bool refreshbacking)
{
   if (!trk)
      return;

   trk = *GetTracks()->FindLeader(trk);
   auto height =
      TrackList::Channels(trk).sum( &Track::GetHeight )
      - kTopInset - kShadowThickness;

   // subtract insets and shadows from the rectangle, but not border
   // This matters because some separators do paint over the border
   wxRect rect(kLeftInset,
            -mViewInfo->vpos + trk->GetY() + kTopInset,
            GetRect().GetWidth() - kLeftInset - kRightInset - kShadowThickness,
            height);

   if( refreshbacking )
   {
      mRefreshBacking = true;
   }

   Refresh( false, &rect );
}


/// This method overrides Refresh() of wxWindow so that the
/// boolean play indictaor can be set to false, so that an old play indicator that is
/// no longer there won't get  XORed (to erase it), thus redrawing it on the
/// TrackPanel
void TrackPanel::Refresh(bool eraseBackground /* = TRUE */,
                         const wxRect *rect /* = NULL */)
{
   // Tell OnPaint() to refresh the backing bitmap.
   //
   // Originally I had the check within the OnPaint() routine and it
   // was working fine.  That was until I found that, even though a full
   // refresh was requested, Windows only set the onscreen portion of a
   // window as damaged.
   //
   // So, if any part of the trackpanel was off the screen, full refreshes
   // didn't work and the display got corrupted.
   if( !rect || ( *rect == GetRect() ) )
   {
      mRefreshBacking = true;
   }
   wxWindow::Refresh(eraseBackground, rect);
   DisplaySelection();
}

#include "TrackPanelDrawingContext.h"

/// Draw the actual track areas.  We only draw the borders
/// and the little buttons and menues and whatnot here, the
/// actual contents of each track are drawn by the TrackArtist.
void TrackPanel::DrawTracks(wxDC * dc)
{
   wxRegion region = GetUpdateRegion();

   const wxRect clip = GetRect();

   wxRect panelRect = clip;
   panelRect.y = -mViewInfo->vpos;

   wxRect tracksRect = panelRect;
   tracksRect.x += GetLabelWidth();
   tracksRect.width -= GetLabelWidth();

   ToolsToolBar *pTtb = mListener->TP_GetToolsToolBar();
   bool bMultiToolDown = pTtb->IsDown(multiTool);
   bool envelopeFlag   = pTtb->IsDown(envelopeTool) || bMultiToolDown;
   bool bigPointsFlag  = pTtb->IsDown(drawTool) || bMultiToolDown;
   bool sliderFlag     = bMultiToolDown;

   TrackPanelDrawingContext context{ *dc, Target(), mLastMouseState };

   // The track artist actually draws the stuff inside each track
   mTrackArtist->DrawTracks(context, GetTracks(),
                            region, tracksRect, clip,
                            mViewInfo->selectedRegion, *mViewInfo,
                            envelopeFlag, bigPointsFlag, sliderFlag);

   DrawEverythingElse(context, region, clip);
}

/// Draws 'Everything else'.  In particular it draws:
///  - Drop shadow for tracks and vertical rulers.
///  - Zooming Indicators.
///  - Fills in space below the tracks.
void TrackPanel::DrawEverythingElse(TrackPanelDrawingContext &context,
                                    const wxRegion &region,
                                    const wxRect & clip)
{
   // We draw everything else
   auto dc = &context.dc;
   wxRect focusRect(-1, -1, 0, 0);
   wxRect trackRect = clip;
   trackRect.height = 0;   // for drawing background in no tracks case.

   for ( auto t :
         GetTracks()->Any< const Track >() + IsVisibleTrack{ GetProject() } ) {
      auto visibleT = t->SubstitutePendingChangedTrack().get();
      trackRect.y = visibleT->GetY() - mViewInfo->vpos;
      trackRect.height = visibleT->GetHeight();

      auto leaderTrack = *GetTracks()->FindLeader( t );
      // If the previous track is linked to this one but isn't on the screen
      // (and thus would have been skipped) we need to draw that track's border
      // instead.
      bool drawBorder = (t == leaderTrack || trackRect.y < 0);

      if (drawBorder) {
         wxRect teamRect = trackRect;
         auto visibleLeaderTrack =
           leaderTrack->SubstitutePendingChangedTrack().get();
         teamRect.y = visibleLeaderTrack->GetY() - mViewInfo->vpos;
         teamRect.height = TrackList::Channels(leaderTrack).sum(
            [&] (const Track *channel) {
               channel = channel->SubstitutePendingChangedTrack().get();
               return channel->GetHeight();
            }
         );

         if (mAx->IsFocused(t)) {
            focusRect = teamRect;
         }
         DrawOutside(context, leaderTrack, teamRect);
      }

      // Believe it or not, we can speed up redrawing if we don't
      // redraw the vertical ruler when only the waveform data has
      // changed.  An example is during recording.

#if DEBUG_DRAW_TIMING
//      wxRect rbox = region.GetBox();
//      wxPrintf(wxT("Update Region: %d %d %d %d\n"),
//             rbox.x, rbox.y, rbox.width, rbox.height);
#endif

      if (region.Contains(0, trackRect.y, GetLeftOffset(), trackRect.height)) {
         wxRect rect = trackRect;
         rect.x += GetVRulerOffset();
         rect.y += kTopMargin;
         rect.width = GetVRulerWidth();
         rect.height -= (kTopMargin + kBottomMargin);
         mTrackArtist->DrawVRuler(context, visibleT, rect);
      }
   }

   auto target = Target();
   if (target)
      target->DrawExtras(UIHandle::Cells, dc, region, clip);

   // Paint over the part below the tracks
   trackRect.y += trackRect.height;
   if (trackRect.y < clip.GetBottom()) {
      AColor::TrackPanelBackground(dc, false);
      dc->DrawRectangle(trackRect.x,
                        trackRect.y,
                        trackRect.width,
                        clip.height - trackRect.y);
   }

   // Sometimes highlight is not drawn on backing bitmap. I thought
   // it was because FindFocus did not return "this" on Mac, but
   // when I removed that test, yielding this condition:
   //     if (GetFocusedTrack() != NULL) {
   // the highlight was reportedly drawn even when something else
   // was the focus and no highlight should be drawn. -RBD
   if (GetFocusedTrack() != NULL && GetProject()->IsFocused( this )) {
      HighlightFocusedTrack(dc, focusRect);
   }

   if (target)
      target->DrawExtras(UIHandle::Panel, dc, region, clip);
}

// Make this #include go away!
#include "tracks/ui/TrackControls.h"

void TrackInfo::DrawItems
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track &track  )
{
   const auto topLines = getTCPLines( track );
   const auto bottomLines = commonTrackTCPBottomLines;
   DrawItems
      ( context, rect, &track, topLines, bottomLines );
}

void TrackInfo::DrawItems
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack,
  const std::vector<TCPLine> &topLines, const std::vector<TCPLine> &bottomLines )
{
   auto dc = &context.dc;
   TrackInfo::SetTrackInfoFont(dc);
   dc->SetTextForeground(theTheme.Colour(clrTrackPanelText));

   {
      int yy = 0;
      for ( const auto &line : topLines ) {
         wxRect itemRect{
            rect.x, rect.y + yy,
            rect.width, line.height
         };
         if ( !TrackInfo::HideTopItem( rect, itemRect ) &&
              line.drawFunction )
            line.drawFunction( context, itemRect, pTrack );
         yy += line.height + line.extraSpace;
      }
   }
   {
      int yy = rect.height;
      for ( const auto &line : bottomLines ) {
         yy -= line.height + line.extraSpace;
         if ( line.drawFunction ) {
            wxRect itemRect{
               rect.x, rect.y + yy,
               rect.width, line.height
            };
            line.drawFunction( context, itemRect, pTrack );
         }
      }
   }
}

#include "tracks/ui/TrackButtonHandles.h"
void TrackInfo::CloseTitleDrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
   auto dc = &context.dc;
   bool selected = pTrack ? pTrack->GetSelected() : true;
   {
      wxRect bev = rect;
      GetCloseBoxHorizontalBounds( rect, bev );
      auto target = dynamic_cast<CloseButtonHandle*>( context.target.get() );
      bool hit = target && target->GetTrack().get() == pTrack;
      bool captured = hit && target->IsClicked();
      bool down = captured && bev.Contains( context.lastState.GetPosition());
      AColor::Bevel2(*dc, !down, bev, selected, hit );

#ifdef EXPERIMENTAL_THEMING
      wxPen pen( theTheme.Colour( clrTrackPanelText ));
      dc->SetPen( pen );
#else
      dc->SetPen(*wxBLACK_PEN);
#endif
      bev.Inflate( -1, -1 );
      // Draw the "X"
      const int s = 6;

      int ls = bev.x + ((bev.width - s) / 2);
      int ts = bev.y + ((bev.height - s) / 2);
      int rs = ls + s;
      int bs = ts + s;

      AColor::Line(*dc, ls,     ts, rs,     bs);
      AColor::Line(*dc, ls + 1, ts, rs + 1, bs);
      AColor::Line(*dc, rs,     ts, ls,     bs);
      AColor::Line(*dc, rs + 1, ts, ls + 1, bs);

      //   bev.Inflate(-1, -1);
   }

   {
      wxRect bev = rect;
      GetTitleBarHorizontalBounds( rect, bev );
      auto target = dynamic_cast<MenuButtonHandle*>( context.target.get() );
      bool hit = target && target->GetTrack().get() == pTrack;
      bool captured = hit && target->IsClicked();
      bool down = captured && bev.Contains( context.lastState.GetPosition());
      wxString titleStr =
         pTrack ? pTrack->GetName() : _("Name");

      //bev.Inflate(-1, -1);
      AColor::Bevel2(*dc, !down, bev, selected, hit);

      // Draw title text
      SetTrackInfoFont(dc);

      // Bug 1660 The 'k' of 'Audio Track' was being truncated.
      // Constant of 32 found by counting pixels on a windows machine.
      // I believe it's the size of the X close button + the size of the 
      // drop down arrow.
      int allowableWidth = rect.width - 32;

      wxCoord textWidth, textHeight;
      dc->GetTextExtent(titleStr, &textWidth, &textHeight);
      while (textWidth > allowableWidth) {
         titleStr = titleStr.Left(titleStr.Length() - 1);
         dc->GetTextExtent(titleStr, &textWidth, &textHeight);
      }

      // Pop-up triangle
   #ifdef EXPERIMENTAL_THEMING
      wxColour c = theTheme.Colour( clrTrackPanelText );
   #else
      wxColour c = *wxBLACK;
   #endif

      // wxGTK leaves little scraps (antialiasing?) of the
      // characters if they are repeatedly drawn.  This
      // happens when holding down mouse button and moving
      // in and out of the title bar.  So clear it first.
   //   AColor::MediumTrackInfo(dc, t->GetSelected());
   //   dc->DrawRectangle(bev);

      dc->SetTextForeground( c );
      dc->SetTextBackground( wxTRANSPARENT );
      dc->DrawText(titleStr, bev.x + 2, bev.y + (bev.height - textHeight) / 2);



      dc->SetPen(c);
      dc->SetBrush(c);

      int s = 10; // Width of dropdown arrow...height is half of width
      AColor::Arrow(*dc,
                    bev.GetRight() - s - 3, // 3 to offset from right border
                    bev.y + ((bev.height - (s / 2)) / 2),
                    s);

   }
}

void TrackInfo::MinimizeSyncLockDrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
   auto dc = &context.dc;
   bool selected = pTrack ? pTrack->GetSelected() : true;
   bool syncLockSelected = pTrack ? pTrack->IsSyncLockSelected() : true;
   bool minimized = pTrack ? pTrack->GetMinimized() : false;
   {
      wxRect bev = rect;
      GetMinimizeHorizontalBounds(rect, bev);
      auto target = dynamic_cast<MinimizeButtonHandle*>( context.target.get() );
      bool hit = target && target->GetTrack().get() == pTrack;
      bool captured = hit && target->IsClicked();
      bool down = captured && bev.Contains( context.lastState.GetPosition());

      // Clear background to get rid of previous arrow
      //AColor::MediumTrackInfo(dc, t->GetSelected());
      //dc->DrawRectangle(bev);

      AColor::Bevel2(*dc, !down, bev, selected, hit);

#ifdef EXPERIMENTAL_THEMING
      wxColour c = theTheme.Colour(clrTrackPanelText);
      dc->SetBrush(c);
      dc->SetPen(c);
#else
      AColor::Dark(dc, selected);
#endif

      AColor::Arrow(*dc,
                    bev.x - 5 + bev.width / 2,
                    bev.y - 2 + bev.height / 2,
                    10,
                    minimized);
   }

   // Draw the sync-lock indicator if this track is in a sync-lock selected group.
   if (syncLockSelected)
   {
      wxRect syncLockIconRect = rect;
	
      GetSyncLockHorizontalBounds( rect, syncLockIconRect );
      wxBitmap syncLockBitmap(theTheme.Image(bmpSyncLockIcon));
      // Icon is 12x12 and syncLockIconRect is 16x16.
      dc->DrawBitmap(syncLockBitmap,
                     syncLockIconRect.x + 3,
                     syncLockIconRect.y + 2,
                     true);
   }
}

#include "tracks/playabletrack/notetrack/ui/NoteTrackButtonHandle.h"
void TrackInfo::MidiControlsDrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
#ifdef EXPERIMENTAL_MIDI_OUT
   auto target = dynamic_cast<NoteTrackButtonHandle*>( context.target.get() );
   bool hit = target && target->GetTrack().get() == pTrack;
   auto channel = hit ? target->GetChannel() : -1;
   auto &dc = context.dc;
   wxRect midiRect = rect;
   GetMidiControlsHorizontalBounds(rect, midiRect);
   NoteTrack::DrawLabelControls
      ( static_cast<const NoteTrack *>(pTrack), dc, midiRect, channel );
#endif // EXPERIMENTAL_MIDI_OUT
}

template<typename TrackClass>
void TrackInfo::SliderDrawFunction
( LWSlider *(*Selector)
    (const wxRect &sliderRect, const TrackClass *t, bool captured, wxWindow*),
  wxDC *dc, const wxRect &rect, const Track *pTrack,
  bool captured, bool highlight )
{
   wxRect sliderRect = rect;
   TrackInfo::GetSliderHorizontalBounds( rect.GetTopLeft(), sliderRect );
   auto wt = static_cast<const TrackClass*>( pTrack );
   Selector( sliderRect, wt, captured, nullptr )->OnPaint(*dc, highlight);
}

#include "tracks/playabletrack/wavetrack/ui/WaveTrackSliderHandles.h"
void TrackInfo::PanSliderDrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
   auto target = dynamic_cast<PanSliderHandle*>( context.target.get() );
   auto dc = &context.dc;
   bool hit = target && target->GetTrack().get() == pTrack;
   bool captured = hit && target->IsClicked();
   SliderDrawFunction<WaveTrack>
      ( &TrackInfo::PanSlider, dc, rect, pTrack, captured, hit);
}

void TrackInfo::GainSliderDrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
   auto target = dynamic_cast<GainSliderHandle*>( context.target.get() );
   auto dc = &context.dc;
   bool hit = target && target->GetTrack().get() == pTrack;
   if( hit )
      hit=hit;
   bool captured = hit && target->IsClicked();
   SliderDrawFunction<WaveTrack>
      ( &TrackInfo::GainSlider, dc, rect, pTrack, captured, hit);
}

#ifdef EXPERIMENTAL_MIDI_OUT
#include "tracks/playabletrack/notetrack/ui/NoteTrackSliderHandles.h"
void TrackInfo::VelocitySliderDrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
   auto dc = &context.dc;
   auto target = dynamic_cast<VelocitySliderHandle*>( context.target.get() );
   bool hit = target && target->GetTrack().get() == pTrack;
   bool captured = hit && target->IsClicked();
   SliderDrawFunction<NoteTrack>
      ( &TrackInfo::VelocitySlider, dc, rect, pTrack, captured, hit);
}
#endif

void TrackInfo::MuteOrSoloDrawFunction
( wxDC *dc, const wxRect &bev, const Track *pTrack, bool down, 
  bool WXUNUSED(captured),
  bool solo, bool hit )
{
   //bev.Inflate(-1, -1);
   bool selected = pTrack ? pTrack->GetSelected() : true;
   auto pt = dynamic_cast<const PlayableTrack *>(pTrack);
   bool value = pt ? (solo ? pt->GetSolo() : pt->GetMute()) : false;

#if 0
   AColor::MediumTrackInfo( dc, t->GetSelected());
   if( solo )
   {
      if( pt && pt->GetSolo() )
      {
         AColor::Solo(dc, pt->GetSolo(), t->GetSelected());
      }
   }
   else
   {
      if( pt && pt->GetMute() )
      {
         AColor::Mute(dc, pt->GetMute(), t->GetSelected(), pt->GetSolo());
      }
   }
   //(solo) ? AColor::Solo(dc, t->GetSolo(), t->GetSelected()) :
   //    AColor::Mute(dc, t->GetMute(), t->GetSelected(), t->GetSolo());
   dc->SetPen( *wxTRANSPARENT_PEN );//No border!
   dc->DrawRectangle(bev);
#endif

   wxCoord textWidth, textHeight;
   wxString str = (solo) ?
      /* i18n-hint: This is on a button that will silence all the other tracks.*/
      _("Solo") :
      /* i18n-hint: This is on a button that will silence this track.*/
      _("Mute");

   AColor::Bevel2(
      *dc,
      value == down,
      bev,
      selected, hit
   );

   SetTrackInfoFont(dc);
   dc->GetTextExtent(str, &textWidth, &textHeight);
   dc->DrawText(str, bev.x + (bev.width - textWidth) / 2, bev.y + (bev.height - textHeight) / 2);
}

#include "tracks/playabletrack/ui/PlayableTrackButtonHandles.h"
void TrackInfo::WideMuteDrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
   auto dc = &context.dc;
   wxRect bev = rect;
   GetWideMuteSoloHorizontalBounds( rect, bev );
   auto target = dynamic_cast<MuteButtonHandle*>( context.target.get() );
   bool hit = target && target->GetTrack().get() == pTrack;
   bool captured = hit && target->IsClicked();
   bool down = captured && bev.Contains( context.lastState.GetPosition());
   MuteOrSoloDrawFunction( dc, bev, pTrack, down, captured, false, hit );
}

void TrackInfo::WideSoloDrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
   auto dc = &context.dc;
   wxRect bev = rect;
   GetWideMuteSoloHorizontalBounds( rect, bev );
   auto target = dynamic_cast<SoloButtonHandle*>( context.target.get() );
   bool hit = target && target->GetTrack().get() == pTrack;
   bool captured = hit && target->IsClicked();
   bool down = captured && bev.Contains( context.lastState.GetPosition());
   MuteOrSoloDrawFunction( dc, bev, pTrack, down, captured, true, hit );
}

void TrackInfo::MuteAndSoloDrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
   auto dc = &context.dc;
   bool bHasSoloButton = TrackPanel::HasSoloButton();

   wxRect bev = rect;
   if ( bHasSoloButton )
      GetNarrowMuteHorizontalBounds( rect, bev );
   else
      GetWideMuteSoloHorizontalBounds( rect, bev );
   {
      auto target = dynamic_cast<MuteButtonHandle*>( context.target.get() );
      bool hit = target && target->GetTrack().get() == pTrack;
      bool captured = hit && target->IsClicked();
      bool down = captured && bev.Contains( context.lastState.GetPosition());
      MuteOrSoloDrawFunction( dc, bev, pTrack, down, captured, false, hit );
   }

   if( !bHasSoloButton )
      return;

   GetNarrowSoloHorizontalBounds( rect, bev );
   {
      auto target = dynamic_cast<SoloButtonHandle*>( context.target.get() );
      bool hit = target && target->GetTrack().get() == pTrack;
      bool captured = hit && target->IsClicked();
      bool down = captured && bev.Contains( context.lastState.GetPosition());
      MuteOrSoloDrawFunction( dc, bev, pTrack, down, captured, true, hit );
   }
}

void TrackInfo::StatusDrawFunction
   ( const wxString &string, wxDC *dc, const wxRect &rect )
{
   static const int offset = 3;
   dc->DrawText(string, rect.x + offset, rect.y);
}

void TrackInfo::Status1DrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
   auto dc = &context.dc;
   auto wt = static_cast<const WaveTrack*>(pTrack);

   /// Returns the string to be displayed in the track label
   /// indicating whether the track is mono, left, right, or
   /// stereo and what sample rate it's using.
   auto rate = wt ? wt->GetRate() : 44100.0;
   wxString s;
   if (!pTrack || TrackList::Channels(pTrack).size() > 1)
      // TODO: more-than-two-channels-message
      // more appropriate strings
      s = _("Stereo, %dHz");
   else {
      if (wt->GetChannel() == Track::MonoChannel)
         s = _("Mono, %dHz");
      else if (wt->GetChannel() == Track::LeftChannel)
         s = _("Left, %dHz");
      else if (wt->GetChannel() == Track::RightChannel)
         s = _("Right, %dHz");
   }
   s = wxString::Format( s, (int) (rate + 0.5) );

   StatusDrawFunction( s, dc, rect );
}

void TrackInfo::Status2DrawFunction
( TrackPanelDrawingContext &context,
  const wxRect &rect, const Track *pTrack )
{
   auto dc = &context.dc;
   auto wt = static_cast<const WaveTrack*>(pTrack);
   auto format = wt ? wt->GetSampleFormat() : floatSample;
   auto s = GetSampleFormatStr(format);
   StatusDrawFunction( s, dc, rect );
}

void TrackPanel::DrawOutside
(TrackPanelDrawingContext &context,
 const Track * t, const wxRect & rec)
{
   auto dc = &context.dc;
   const auto wt = track_cast<const WaveTrack*>(t);

   // Draw things that extend right of track control panel
   {
      // Start with whole track rect
      wxRect rect = rec;
      DrawOutsideOfTrack(context, t, rect);

      {
         auto channels = TrackList::Channels(t);
         // omit last (perhaps, only) channel
         --channels.second;
         for (auto channel : channels)
            // draw the sash below this channel
            DrawSash(channel, dc, rect);
      }

      // Now exclude left, right, and top insets
      rect.x += kLeftInset;
      rect.y += kTopInset;
      rect.width -= kLeftInset * 2;
      rect.height -= kTopInset;

      int labelw = GetLabelWidth();
      int vrul = GetVRulerOffset();
      mTrackInfo.DrawBackground(dc, rect, t->GetSelected(), (wt != nullptr), labelw, vrul);

      // Vaughan, 2010-08-24: No longer doing this.
      // Draw sync-lock tiles in ruler area.
      //if (t->IsSyncLockSelected()) {
      //   wxRect tileFill = rect;
      //   tileFill.x = GetVRulerOffset();
      //   tileFill.width = GetVRulerWidth();
      //   TrackArtist::DrawSyncLockTiles(dc, tileFill);
      //}

      DrawBordersAroundTrack(dc, rect, vrul);
      {
         auto channels = TrackList::Channels(t);
         // omit last (perhaps, only) channel
         --channels.second;
         for (auto channel : channels)
            // draw the sash below this channel
            DrawBordersAroundSash(channel, dc, rect, labelw);
      }

      DrawShadow(t, dc, rect);
   }

   // Draw things within the track control panel
   wxRect rect = rec;
   rect.x += kLeftMargin;
   rect.width = kTrackInfoWidth - kLeftMargin;
   rect.y += kTopMargin;
   rect.height -= (kBottomMargin + kTopMargin);

   TrackInfo::DrawItems( context, rect, *t );

   //mTrackInfo.DrawBordersWithin( dc, rect, *t );
}

// Given rectangle should be the whole track rectangle
// Paint the inset areas left, top, and right in a background color
// If linked to a following channel, also paint the separator area, which
// overlaps the next track rectangle's top
void TrackPanel::DrawOutsideOfTrack
(TrackPanelDrawingContext &context, const Track * t, const wxRect & rect)
{
   auto dc = &context.dc;

   // Fill in area outside of the track
   AColor::TrackPanelBackground(dc, false);
   wxRect side;

   // Area between panel border and left track border
   side = rect;
   side.width = kLeftInset;
   dc->DrawRectangle(side);

   // Area between panel border and top track border
   side = rect;
   side.height = kTopInset;
   dc->DrawRectangle(side);

   // Area between panel border and right track border
   side = rect;
   side.x += side.width - kRightInset;
   side.width = kRightInset;
   dc->DrawRectangle(side);
}

void TrackPanel::DrawSash(const Track * t, wxDC * dc, const wxRect & rect)
{
   // Area between channels of a group
   // Paint the channel separator over (what would be) the shadow of this
   // channel, and the top inset of the following channel
   wxRect side = rect;
   side.y = t->GetY() - mViewInfo->vpos + t->GetHeight() - kShadowThickness;
   side.height = kTopInset + kShadowThickness;
   dc->DrawRectangle(side);
}

void TrackPanel::SetBackgroundCell
(const std::shared_ptr< TrackPanelCell > &pCell)
{
   mpBackground = pCell;
}

std::shared_ptr< TrackPanelCell > TrackPanel::GetBackgroundCell()
{
   return mpBackground;
}

/// Draw a three-level highlight gradient around the focused track.
void TrackPanel::HighlightFocusedTrack(wxDC * dc, const wxRect & rect)
{
   wxRect theRect = rect;
   theRect.x += kLeftInset;
   theRect.y += kTopInset;
   theRect.width -= kLeftInset * 2;
   theRect.height -= kTopInset;

   dc->SetBrush(*wxTRANSPARENT_BRUSH);

   AColor::TrackFocusPen(dc, 0);
   dc->DrawRectangle(theRect.x - 1, theRect.y - 1, theRect.width + 2, theRect.height + 2);

   AColor::TrackFocusPen(dc, 1);
   dc->DrawRectangle(theRect.x - 2, theRect.y - 2, theRect.width + 4, theRect.height + 4);

   AColor::TrackFocusPen(dc, 2);
   dc->DrawRectangle(theRect.x - 3, theRect.y - 3, theRect.width + 6, theRect.height + 6);
}

void TrackPanel::UpdateVRulers()
{
   for (auto t : GetTracks()->Any< const WaveTrack >())
      UpdateTrackVRuler(t);

   UpdateVRulerSize();
}

void TrackPanel::UpdateVRuler(Track *t)
{
   if (t)
      UpdateTrackVRuler(t);

   UpdateVRulerSize();
}

void TrackPanel::UpdateTrackVRuler(const Track *t)
{
   wxASSERT(t);
   if (!t)
      return;

   wxRect rect(GetVRulerOffset(),
            kTopMargin,
            GetVRulerWidth(),
            0);


   for (auto channel : TrackList::Channels(t)) {
      rect.height = channel->GetHeight() - (kTopMargin + kBottomMargin);
      mTrackArtist->UpdateVRuler(channel, rect);
   }
}

void TrackPanel::UpdateVRulerSize()
{
   auto trackRange = GetTracks()->Any();
   if (trackRange) {
      wxSize s { 0, 0 };
      for (auto t : trackRange)
         s.IncTo(t->vrulerSize);

      if (vrulerSize != s) {
         vrulerSize = s;
         mRuler->SetLeftOffset(GetLeftOffset());  // bevel on AdornedRuler
         mRuler->Refresh();
      }
   }
   Refresh(false);
}

// Make sure selection edge is in view
void TrackPanel::ScrollIntoView(double pos)
{
   int w;
   GetTracksUsableArea( &w, NULL );

   int pixel = mViewInfo->TimeToPosition(pos);
   if (pixel < 0 || pixel >= w)
   {
      mListener->TP_ScrollWindow
         (mViewInfo->OffsetTimeByPixels(pos, -(w / 2)));
      Refresh(false);
   }
}

void TrackPanel::ScrollIntoView(int x)
{
   ScrollIntoView(mViewInfo->PositionToTime(x, GetLeftOffset()));
}

void TrackPanel::OnTrackMenu(Track *t)
{
   CellularPanel::DoContextMenu( t );
}

Track * TrackPanel::GetFirstSelectedTrack()
{
   auto t = *GetTracks()->Selected().begin();
   if (t)
      return t;
   else
      //if nothing is selected, return the first track
      return *GetTracks()->Any().begin();
}

void TrackPanel::EnsureVisible(Track * t)
{
   SetFocusedTrack(t);

   int trackTop = 0;
   int trackHeight =0;

   for (auto it : GetTracks()->Leaders()) {
      trackTop += trackHeight;

      auto channels = TrackList::Channels(it);
      trackHeight = channels.sum( &Track::GetHeight );

      //We have found the track we want to ensure is visible.
      if (channels.contains(t)) {

         //Get the size of the trackpanel.
         int width, height;
         GetSize(&width, &height);

         if (trackTop < mViewInfo->vpos) {
            height = mViewInfo->vpos - trackTop + mViewInfo->scrollStep;
            height /= mViewInfo->scrollStep;
            mListener->TP_ScrollUpDown(-height);
         }
         else if (trackTop + trackHeight > mViewInfo->vpos + height) {
            height = (trackTop + trackHeight) - (mViewInfo->vpos + height);
            height = (height + mViewInfo->scrollStep + 1) / mViewInfo->scrollStep;
            mListener->TP_ScrollUpDown(height);
         }

         break;
      }
   }
   Refresh(false);
}

// 0.0 scrolls to top
// 1.0 scrolls to bottom.
void TrackPanel::VerticalScroll( float fracPosition){

   int trackTop = 0;
   int trackHeight = 0;

   auto tracks = GetTracks();
   auto GetHeight =
      [&]( const Track *t ){ return tracks->GetGroupHeight(t); };

   auto range = tracks->Leaders();
   if (!range.empty()) {
      trackHeight = GetHeight( *range.rbegin() );
      --range.second;
   }
   trackTop = range.sum( GetHeight );

   int delta;
   
   //Get the size of the trackpanel.
   int width, height;
   GetSize(&width, &height);

   delta = (fracPosition * (trackTop + trackHeight - height)) - mViewInfo->vpos + mViewInfo->scrollStep;
   //wxLogDebug( "Scroll down by %i pixels", delta );
   delta /= mViewInfo->scrollStep;
   mListener->TP_ScrollUpDown(delta);
   Refresh(false);
}


// Given rectangle excludes the insets left, right, and top
// Draw a rectangular border and also a vertical separator of track controls
// from the rest (ruler and proper track area)
void TrackPanel::DrawBordersAroundTrack(wxDC * dc,
                                        const wxRect & rect,
                                        const int vrul)
{
   // Border around track and label area
   // leaving room for the shadow
   dc->SetBrush(*wxTRANSPARENT_BRUSH);
   dc->SetPen(*wxBLACK_PEN);
   dc->DrawRectangle(rect.x, rect.y,
                     rect.width - kShadowThickness,
                     rect.height - kShadowThickness);


   // between vruler and TrackInfo
   AColor::Line(*dc, vrul, rect.y, vrul, rect.y + rect.height - 1);
}

void TrackPanel::DrawBordersAroundSash(const Track * t, wxDC * dc,
                                        const wxRect & rect, const int labelw)
{
   int h1 = t->GetY() - mViewInfo->vpos + t->GetHeight();
   // h1 is the top coordinate of the following channel's rectangle
   // Draw (part of) the bottom border of the top channel and top border of the bottom
   // At left it extends between the vertical rulers too
   // These lines stroke over what is otherwise "border" of each channel
   AColor::Line(*dc, labelw, h1 - kBottomMargin, rect.x + rect.width - 1, h1 - kBottomMargin);
   AColor::Line(*dc, labelw, h1 + kTopInset, rect.x + rect.width - 1, h1 + kTopInset);
}

// Given rectangle has insets subtracted left, right, and top
// Stroke lines along bottom and right, which are slightly short at
// bottom-left and top-right
void TrackPanel::DrawShadow(const Track * /* t */ , wxDC * dc, const wxRect & rect)
{
   int right = rect.x + rect.width - 1;
   int bottom = rect.y + rect.height - 1;

   // shadow color for lines
   dc->SetPen(*wxBLACK_PEN);

   // bottom
   AColor::Line(*dc, rect.x, bottom, right, bottom);
   // right
   AColor::Line(*dc, right, rect.y, right, bottom);

   // background color erases small parts of those lines
   AColor::TrackPanelBackground(dc, false);

   // bottom-left
   AColor::Line(*dc, rect.x, bottom, rect.x + 1, bottom);
   // top-right
   AColor::Line(*dc, right, rect.y, right, rect.y + 1);
}

/// Determines which cell is under the mouse
///  @param mouseX - mouse X position.
///  @param mouseY - mouse Y position.
auto TrackPanel::FindCell(int mouseX, int mouseY) -> FoundCell
{
   auto range = Cells();
   auto &iter = range.first, &end = range.second;
   while
      ( iter != end &&
        !(*iter).second.Contains( mouseX, mouseY ) )
      ++iter;
   if (iter == end)
      return {};

   auto found = *iter;
   return {
      found.first,
      found.second
   };
}

wxRect TrackPanel::FindRect( const TrackPanelCell &cell )
{
   auto range = Cells();
   auto end = range.second,
      iter = std::find_if( range.first, end,
         [&]( const decltype(*end) &pair )
            { return pair.first.get() == &cell; }
      );
   if (iter == end)
      return {};
   else
      return (*iter).second;
}

// This finds the rectangle of a given track (including all channels),
// either that of the label 'adornment' or the track itself
// The given track is assumed to be the first channel
wxRect TrackPanel::FindTrackRect( const Track * target, bool label )
{
   if (!target) {
      return { 0, 0, 0, 0 };
   }

   // PRL:  I think the following very old comment misused the term "race
   // condition" for a bug that happened with only a single thread.  I think the
   // real problem referred to, was that this function could be reached, via
   // TrackPanelAx callbacks, during low-level operations while the TrackList
   // was not in a consistent state.
   // Now the problem is fixed by delaying the handling of events generated
   // by TrackList.  And besides that, we use Channels() instead of looking
   // directly at the links.

   // Old comment:
   // The check for a null linked track is necessary because there's
   // a possible race condition between the time the 2 linked tracks
   // are added and when wxAccessible methods are called.  This is
   // most evident when using Jaws.
   auto height = TrackList::Channels( target ).sum( &Track::GetHeight );

   wxRect rect{
      0,
      target->GetY() - mViewInfo->vpos,
      GetSize().GetWidth(),
      height
   };


   rect.x += kLeftMargin;
   if (label)
      rect.width = GetVRulerOffset() - kLeftMargin;
   else
      rect.width -= (kLeftMargin + kRightMargin);

   rect.y += kTopMargin;
   rect.height -= (kTopMargin + kBottomMargin);

   return rect;
}

int TrackPanel::GetVRulerWidth() const
{
   return vrulerSize.x;
}

/// Displays the bounds of the selection in the status bar.
void TrackPanel::DisplaySelection()
{
   if (!mListener)
      return;

   // DM: Note that the Selection Bar can actually MODIFY the selection
   // if snap-to mode is on!!!
   mListener->TP_DisplaySelection();
}

TrackPanelCell *TrackPanel::GetFocusedCell()
{
   return mAx->GetFocus().get();
}

Track *TrackPanel::GetFocusedTrack()
{
   return static_cast<Track*>( GetFocusedCell() );
}

void TrackPanel::SetFocusedCell()
{
   SetFocusedTrack( GetFocusedTrack() );
}

void TrackPanel::SetFocusedTrack( Track *t )
{
   // Make sure we always have the first linked track of a stereo track
   t = *GetTracks()->FindLeader(t);

   auto cell = mAx->SetFocus( Track::Pointer( t ) ).get();

   if (cell) {
      AudacityProject::CaptureKeyboard(this);
      Refresh( false );
   }
}

/**********************************************************************

  TrackInfo code is destined to move out of this file.
  Code should become a lot cleaner when we have sizers.

**********************************************************************/

TrackInfo::TrackInfo(TrackPanel * pParentIn)
{
   pParent = pParentIn;

   ReCreateSliders();

   UpdatePrefs();
}

TrackInfo::~TrackInfo()
{
}

void TrackInfo::ReCreateSliders(){
   const wxPoint point{ 0, 0 };
   wxRect sliderRect;
   GetGainRect(point, sliderRect);

   float defPos = 1.0;
   /* i18n-hint: Title of the Gain slider, used to adjust the volume */
   gGain = std::make_unique<LWSlider>(pParent, _("Gain"),
                        wxPoint(sliderRect.x, sliderRect.y),
                        wxSize(sliderRect.width, sliderRect.height),
                        DB_SLIDER);
   gGain->SetDefaultValue(defPos);

   gGainCaptured = std::make_unique<LWSlider>(pParent, _("Gain"),
                                wxPoint(sliderRect.x, sliderRect.y),
                                wxSize(sliderRect.width, sliderRect.height),
                                DB_SLIDER);
   gGainCaptured->SetDefaultValue(defPos);

   GetPanRect(point, sliderRect);

   defPos = 0.0;
   /* i18n-hint: Title of the Pan slider, used to move the sound left or right */
   gPan = std::make_unique<LWSlider>(pParent, _("Pan"),
                       wxPoint(sliderRect.x, sliderRect.y),
                       wxSize(sliderRect.width, sliderRect.height),
                       PAN_SLIDER);
   gPan->SetDefaultValue(defPos);

   gPanCaptured = std::make_unique<LWSlider>(pParent, _("Pan"),
                               wxPoint(sliderRect.x, sliderRect.y),
                               wxSize(sliderRect.width, sliderRect.height),
                               PAN_SLIDER);
   gPanCaptured->SetDefaultValue(defPos);

#ifdef EXPERIMENTAL_MIDI_OUT
   GetVelocityRect(point, sliderRect);

   /* i18n-hint: Title of the Velocity slider, used to adjust the volume of note tracks */
   gVelocity = std::make_unique<LWSlider>(pParent, _("Velocity"),
      wxPoint(sliderRect.x, sliderRect.y),
      wxSize(sliderRect.width, sliderRect.height),
      VEL_SLIDER);
   gVelocity->SetDefaultValue(0.0);
   gVelocityCaptured = std::make_unique<LWSlider>(pParent, _("Velocity"),
      wxPoint(sliderRect.x, sliderRect.y),
      wxSize(sliderRect.width, sliderRect.height),
      VEL_SLIDER);
   gVelocityCaptured->SetDefaultValue(0.0);
#endif

}

int TrackInfo::GetTrackInfoWidth() const
{
   return kTrackInfoWidth;
}

void TrackInfo::GetCloseBoxHorizontalBounds( const wxRect & rect, wxRect &dest )
{
   dest.x = rect.x;
   dest.width = kTrackInfoBtnSize;
}

void TrackInfo::GetCloseBoxRect(const wxRect & rect, wxRect & dest)
{
   GetCloseBoxHorizontalBounds( rect, dest );
   auto results = CalcItemY( commonTrackTCPLines, kItemBarButtons );
   dest.y = rect.y + results.first;
   dest.height = results.second;
}

static const int TitleSoloBorderOverlap = 1;

void TrackInfo::GetTitleBarHorizontalBounds( const wxRect & rect, wxRect &dest )
{
   // to right of CloseBoxRect, plus a little more
   wxRect closeRect;
   GetCloseBoxHorizontalBounds( rect, closeRect );
   dest.x = rect.x + closeRect.width + 1;
   dest.width = rect.x + rect.width - dest.x + TitleSoloBorderOverlap;
}

void TrackInfo::GetTitleBarRect(const wxRect & rect, wxRect & dest)
{
   GetTitleBarHorizontalBounds( rect, dest );
   auto results = CalcItemY( commonTrackTCPLines, kItemBarButtons );
   dest.y = rect.y + results.first;
   dest.height = results.second;
}

void TrackInfo::GetNarrowMuteHorizontalBounds( const wxRect & rect, wxRect &dest )
{
   dest.x = rect.x;
   dest.width = rect.width / 2 + 1;
}

void TrackInfo::GetNarrowSoloHorizontalBounds( const wxRect & rect, wxRect &dest )
{
   wxRect muteRect;
   GetNarrowMuteHorizontalBounds( rect, muteRect );
   dest.x = rect.x + muteRect.width;
   dest.width = rect.width - muteRect.width + TitleSoloBorderOverlap;
}

void TrackInfo::GetWideMuteSoloHorizontalBounds( const wxRect & rect, wxRect &dest )
{
   // Larger button, symmetrically placed intended.
   // On windows this gives 15 pixels each side.
   dest.width = rect.width - 2 * kTrackInfoBtnSize + 6;
   dest.x = rect.x + kTrackInfoBtnSize -3;
}

void TrackInfo::GetMuteSoloRect
(const wxRect & rect, wxRect & dest, bool solo, bool bHasSoloButton,
 const Track *pTrack)
{

   auto resultsM = CalcItemY( getTCPLines( *pTrack ), kItemMute );
   auto resultsS = CalcItemY( getTCPLines( *pTrack ), kItemSolo );
   dest.height = resultsS.second;

   int yMute = resultsM.first;
   int ySolo = resultsS.first;

   bool bSameRow = ( yMute == ySolo );
   bool bNarrow = bSameRow && bHasSoloButton;

   if( bNarrow )
   {
      if( solo )
         GetNarrowSoloHorizontalBounds( rect, dest );
      else
         GetNarrowMuteHorizontalBounds( rect, dest );
   }
   else
      GetWideMuteSoloHorizontalBounds( rect, dest );

   if( bSameRow || !solo )
      dest.y = rect.y + yMute;
   else
      dest.y = rect.y + ySolo;

}

void TrackInfo::GetSliderHorizontalBounds( const wxPoint &topleft, wxRect &dest )
{
   dest.x = topleft.x + 6;
   dest.width = kTrackInfoSliderWidth;
}

void TrackInfo::GetGainRect(const wxPoint &topleft, wxRect & dest)
{
   GetSliderHorizontalBounds( topleft, dest );
   auto results = CalcItemY( waveTrackTCPLines, kItemGain );
   dest.y = topleft.y + results.first;
   dest.height = results.second;
}

void TrackInfo::GetPanRect(const wxPoint &topleft, wxRect & dest)
{
   GetGainRect( topleft, dest );
   auto results = CalcItemY( waveTrackTCPLines, kItemPan );
   dest.y = topleft.y + results.first;
}

#ifdef EXPERIMENTAL_MIDI_OUT
void TrackInfo::GetVelocityRect(const wxPoint &topleft, wxRect & dest)
{
   GetSliderHorizontalBounds( topleft, dest );
   auto results = CalcItemY( noteTrackTCPLines, kItemVelocity );
   dest.y = topleft.y + results.first;
   dest.height = results.second;
}
#endif

void TrackInfo::GetMinimizeHorizontalBounds( const wxRect &rect, wxRect &dest )
{
   const int space = 0;// was 3.
   dest.x = rect.x + space;

   wxRect syncLockRect;
   GetSyncLockHorizontalBounds( rect, syncLockRect );

   // Width is rect.width less space on left for track select
   // and on right for sync-lock icon.
   dest.width = rect.width - (space + syncLockRect.width);
}

void TrackInfo::GetMinimizeRect(const wxRect & rect, wxRect &dest)
{
   GetMinimizeHorizontalBounds( rect, dest );
   auto results = CalcBottomItemY
      ( commonTrackTCPBottomLines, kItemMinimize, rect.height);
   dest.y = rect.y + results.first;
   dest.height = results.second;
}

void TrackInfo::GetSyncLockHorizontalBounds( const wxRect &rect, wxRect &dest )
{
   dest.width = kTrackInfoBtnSize;
   dest.x = rect.x + rect.width - dest.width;
}

void TrackInfo::GetSyncLockIconRect(const wxRect & rect, wxRect &dest)
{
   GetSyncLockHorizontalBounds( rect, dest );
   auto results = CalcBottomItemY
      ( commonTrackTCPBottomLines, kItemSyncLock, rect.height);
   dest.y = rect.y + results.first;
   dest.height = results.second;
}

#ifdef USE_MIDI
void TrackInfo::GetMidiControlsHorizontalBounds
( const wxRect &rect, wxRect &dest )
{
   dest.x = rect.x + 1; // To center slightly
   // PRL: TODO: kMidiCellWidth is defined in terms of the other constant
   // kTrackInfoWidth but I am trying to avoid use of that constant.
   // Can cell width be computed from dest.width instead?
   dest.width = kMidiCellWidth * 4;
}

void TrackInfo::GetMidiControlsRect(const wxRect & rect, wxRect & dest)
{
   GetMidiControlsHorizontalBounds( rect, dest );
   auto results = CalcItemY( noteTrackTCPLines, kItemMidiControlsRect );
   dest.y = rect.y + results.first;
   dest.height = results.second;
}
#endif

wxFont TrackInfo::gFont;

/// \todo Probably should move to 'Utils.cpp'.
void TrackInfo::SetTrackInfoFont(wxDC * dc)
{
   dc->SetFont(gFont);
}

void TrackInfo::DrawBordersWithin
   ( wxDC* dc, const wxRect & rect, const Track &track ) const
{
   AColor::Dark(dc, false); // same color as border of toolbars (ToolBar::OnPaint())

   // below close box and title bar
   wxRect buttonRect;
   GetTitleBarRect( rect, buttonRect );
   AColor::Line
      (*dc, rect.x,              buttonRect.y + buttonRect.height,
            rect.width - 1,      buttonRect.y + buttonRect.height);

   // between close box and title bar
   AColor::Line
      (*dc, buttonRect.x, buttonRect.y,
            buttonRect.x, buttonRect.y + buttonRect.height - 1);

   GetMuteSoloRect( rect, buttonRect, false, true, &track );

   bool bHasMuteSolo = dynamic_cast<const PlayableTrack*>( &track ) != NULL;
   if( bHasMuteSolo && !TrackInfo::HideTopItem( rect, buttonRect ) )
   {
      // above mute/solo
      AColor::Line
         (*dc, rect.x,          buttonRect.y,
               rect.width - 1,  buttonRect.y);

      // between mute/solo
      // Draw this little line; if there is no solo, wide mute button will
      // overpaint it later:
      AColor::Line
         (*dc, buttonRect.x + buttonRect.width, buttonRect.y,
               buttonRect.x + buttonRect.width, buttonRect.y + buttonRect.height - 1);

      // below mute/solo
      AColor::Line
         (*dc, rect.x,          buttonRect.y + buttonRect.height,
               rect.width - 1,  buttonRect.y + buttonRect.height);
   }

   // left of and above minimize button
   wxRect minimizeRect;
   this->GetMinimizeRect(rect, minimizeRect);
   AColor::Line
      (*dc, minimizeRect.x - 1, minimizeRect.y,
            minimizeRect.x - 1, minimizeRect.y + minimizeRect.height - 1);
   AColor::Line
      (*dc, minimizeRect.x,                          minimizeRect.y - 1,
            minimizeRect.x + minimizeRect.width - 1, minimizeRect.y - 1);
}

//#define USE_BEVELS

// Paint the whole given rectangle some fill color
void TrackInfo::DrawBackground(wxDC * dc, const wxRect & rect, bool bSelected,
   bool bHasMuteSolo, const int labelw, const int vrul) const
{
   //compiler food.
   static_cast<void>(bHasMuteSolo);
   static_cast<void>(vrul);

   // fill in label
   wxRect fill = rect;
   fill.width = labelw - kLeftInset;
   AColor::MediumTrackInfo(dc, bSelected);
   dc->DrawRectangle(fill);

#ifdef USE_BEVELS
   // This branch is not now used
   // PRL:  todo:  banish magic numbers
   if( bHasMuteSolo )
   {
      int ylast = rect.height-20;
      int ybutton = wxMin(32,ylast-17);
      int ybuttonEnd = 67;

      fill=wxRect( rect.x+1, rect.y+17, vrul-6, ybutton);
      AColor::BevelTrackInfo( *dc, true, fill );
   
      if( ybuttonEnd < ylast ){
         fill=wxRect( rect.x+1, rect.y+ybuttonEnd, fill.width, ylast - ybuttonEnd);
         AColor::BevelTrackInfo( *dc, true, fill );
      }
   }
   else
   {
      fill=wxRect( rect.x+1, rect.y+17, vrul-6, rect.height-37);
      AColor::BevelTrackInfo( *dc, true, fill );
   }
#endif
}

namespace {
unsigned DefaultTrackHeight( const TCPLines &topLines )
{
   int needed =
      kTopMargin + kBottomMargin +
      totalTCPLines( topLines, true ) +
      totalTCPLines( commonTrackTCPBottomLines, false ) + 1;
   return (unsigned) std::max( needed, (int) Track::DefaultHeight );
}
}

unsigned TrackInfo::DefaultNoteTrackHeight()
{
   return DefaultTrackHeight( noteTrackTCPLines );
}

unsigned TrackInfo::DefaultWaveTrackHeight()
{
   return DefaultTrackHeight( waveTrackTCPLines );
}

std::unique_ptr<LWSlider>
   TrackInfo::gGainCaptured
   , TrackInfo::gPanCaptured
   , TrackInfo::gGain
   , TrackInfo::gPan
#ifdef EXPERIMENTAL_MIDI_OUT
   , TrackInfo::gVelocityCaptured
   , TrackInfo::gVelocity
#endif
;

LWSlider * TrackInfo::GainSlider
(const wxRect &sliderRect, const WaveTrack *t, bool captured, wxWindow *pParent)
{
   wxPoint pos = sliderRect.GetPosition();
   float gain = t ? t->GetGain() : 1.0;

   gGain->Move(pos);
   gGain->Set(gain);
   gGainCaptured->Move(pos);
   gGainCaptured->Set(gain);

   auto slider = (captured ? gGainCaptured : gGain).get();
   slider->SetParent( pParent ? pParent : ::GetActiveProject() );
   return slider;
}

LWSlider * TrackInfo::PanSlider
(const wxRect &sliderRect, const WaveTrack *t, bool captured, wxWindow *pParent)
{
   wxPoint pos = sliderRect.GetPosition();
   float pan = t ? t->GetPan() : 0.0;

   gPan->Move(pos);
   gPan->Set(pan);
   gPanCaptured->Move(pos);
   gPanCaptured->Set(pan);

   auto slider = (captured ? gPanCaptured : gPan).get();
   slider->SetParent( pParent ? pParent : ::GetActiveProject() );
   return slider;
}

#ifdef EXPERIMENTAL_MIDI_OUT
LWSlider * TrackInfo::VelocitySlider
(const wxRect &sliderRect, const NoteTrack *t, bool captured, wxWindow *pParent)
{
   wxPoint pos = sliderRect.GetPosition();
   float velocity = t ? t->GetVelocity() : 0.0;

   gVelocity->Move(pos);
   gVelocity->Set(velocity);
   gVelocityCaptured->Move(pos);
   gVelocityCaptured->Set(velocity);

   auto slider = (captured ? gVelocityCaptured : gVelocity).get();
   slider->SetParent( pParent ? pParent : ::GetActiveProject() );
   return slider;
}
#endif

void TrackInfo::UpdatePrefs()
{
   // Calculation of best font size depends on language, so it should be redone in case
   // the language preference changed.

   int fontSize = 10;
   gFont.Create(fontSize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);

   int allowableWidth = GetTrackInfoWidth() - 2; // 2 to allow for left/right borders
   int textWidth, textHeight;
   do {
      gFont.SetPointSize(fontSize);
      pParent->GetTextExtent(_("Stereo, 999999Hz"),
                             &textWidth,
                             &textHeight,
                             NULL,
                             NULL,
                             &gFont);
      fontSize--;
   } while (textWidth >= allowableWidth);
}

IteratorRange< TrackPanelCellIterator > TrackPanel::Cells()
{
   return {
      TrackPanelCellIterator( this, true ),
      TrackPanelCellIterator( this, false )
   };
}

TrackPanelCellIterator::TrackPanelCellIterator(TrackPanel *trackPanel, bool begin)
   : mPanel{ trackPanel }
   , mIter{
        trackPanel->GetTracks()->Any().begin()
           .Filter( IsVisibleTrack( trackPanel->GetProject() ) )
     }
{
   if (begin) {
      mpTrack = Track::Pointer( *mIter );
      if (mpTrack)
         mpCell = mpTrack;
      else
         mpCell = trackPanel->GetBackgroundCell();
   }
   else
      mDidBackground = true;

   const auto size = mPanel->GetSize();
   mRect = { 0, 0, size.x, size.y };
   UpdateRect();
}

TrackPanelCellIterator &TrackPanelCellIterator::operator++ ()
{
   if ( mpTrack ) {
      if ( ++ mType == CellType::Background )
         mType = CellType::Track, mpTrack = Track::Pointer( * ++ mIter );
   }
   if ( mpTrack ) {
      if ( mType == CellType::Label &&
           !mpTrack->IsLeader() )
         // Visit label of stereo track only once
         ++mType;
      switch ( mType ) {
         case CellType::Track:
            mpCell = mpTrack;
            break;
         case CellType::Label:
            mpCell = mpTrack->GetTrackControl();
            break;
         case CellType::VRuler:
            mpCell = mpTrack->GetVRulerControl();
            break;
         case CellType::Resizer: {
            mpCell = mpTrack->GetResizer();
            break;
         }
         default:
            // should not happen
            mpCell.reset();
            break;
      }
   }
   else if ( !mDidBackground )
      mpCell = mPanel->GetBackgroundCell(), mDidBackground = true;
   else
      mpCell.reset();

   UpdateRect();

   return *this;
}

TrackPanelCellIterator TrackPanelCellIterator::operator++ (int)
{
   TrackPanelCellIterator copy(*this);
   ++ *this;
   return copy;
}

auto TrackPanelCellIterator::operator* () const -> value_type
{
   return { mpCell, mRect };
}

void TrackPanelCellIterator::UpdateRect()
{
   const auto size = mPanel->GetSize();
   if ( mpTrack ) {
      mRect = {
         0,
         mpTrack->GetY() - mPanel->GetViewInfo()->vpos,
         size.x,
         mpTrack->GetHeight()
      };
      switch ( mType ) {
         case CellType::Track:
            mRect.x = mPanel->GetLeftOffset();
            mRect.width -= (mRect.x + kRightMargin);
            mRect.y += kTopMargin;
            mRect.height -= (kBottomMargin + kTopMargin);
            break;
         case CellType::Label: {
            mRect.x = kLeftMargin;
            mRect.width = kTrackInfoWidth - mRect.x;
            mRect.y += kTopMargin;
            mRect.height =
               TrackList::Channels(mpTrack.get())
                  .sum( &Track::GetHeight );
            mRect.height -= (kBottomMargin + kTopMargin);
            break;
         }
         case CellType::VRuler:
            {
               mRect.x = kTrackInfoWidth;
               // Right edge of the VRuler is inactive.
               mRect.width = mPanel->GetLeftOffset() - mRect.x;
               mRect.y += kTopMargin;
               mRect.height -= (kBottomMargin + kTopMargin);
            }
            break;
         case CellType::Resizer: {
            // The resizer region encompasses the bottom margin proper to this
            // track, plus the top margin of the next track (or, an equally
            // tall zone below, in case there is no next track)
            if ( mpTrack.get() ==
                *TrackList::Channels(mpTrack.get()).rbegin() )
               // Last channel has a resizer extending farther leftward
               mRect.x = kLeftMargin;
            else
               mRect.x = kTrackInfoWidth;
            mRect.width -= (mRect.x + kRightMargin);
            mRect.y += (mRect.height - kBottomMargin);
            mRect.height = (kBottomMargin + kTopMargin);
            break;
         }
         default:
            // should not happen
            break;
      }
   }
   else if ( mpCell ) {
      // Find a disjoint, maybe empty, rectangle
      // for the empty space appearing at bottom

      mRect.x = kLeftMargin;
      mRect.width = size.x - (mRect.x + kRightMargin);

      // Use previous value of the bottom, either the whole area if
      // there were no tracks, or else the resizer of the last track
      mRect.y =
         std::min( size.y,
            std::max( 0,
               mRect.y + mRect.height ) );
      mRect.height = size.y - mRect.y;
   }
   else
      mRect = {};
}

static TrackPanel * TrackPanelFactory(wxWindow * parent,
   wxWindowID id,
   const wxPoint & pos,
   const wxSize & size,
   const std::shared_ptr<TrackList> &tracks,
   ViewInfo * viewInfo,
   TrackPanelListener * listener,
   AdornedRulerPanel * ruler)
{
   wxASSERT(parent); // to justify safenew
   return safenew TrackPanel(
      parent,
      id,
      pos,
      size,
      tracks,
      viewInfo,
      listener,
      ruler);
}


// Declare the static factory function.
// We defined it in the class.
TrackPanel *(*TrackPanel::FactoryFunction)(
              wxWindow * parent,
              wxWindowID id,
              const wxPoint & pos,
              const wxSize & size,
              const std::shared_ptr<TrackList> &tracks,
              ViewInfo * viewInfo,
              TrackPanelListener * listener,
              AdornedRulerPanel * ruler) = TrackPanelFactory;

TrackPanelCell::~TrackPanelCell()
{
}

HitTestPreview TrackPanelCell::DefaultPreview
(const TrackPanelMouseState &, const AudacityProject *)
{
   return {};
}

unsigned TrackPanelCell::HandleWheelRotation
(const TrackPanelMouseEvent &, AudacityProject *)
{
   return RefreshCode::Cancelled;
}

unsigned TrackPanelCell::DoContextMenu
   (const wxRect &, wxWindow*, wxPoint *)
{
   return RefreshCode::RefreshNone;
}

unsigned TrackPanelCell::CaptureKey(wxKeyEvent &event, ViewInfo &, wxWindow *)
{
   event.Skip();
   return RefreshCode::RefreshNone;
}

unsigned TrackPanelCell::KeyDown(wxKeyEvent &event, ViewInfo &, wxWindow *)
{
   event.Skip();
   return RefreshCode::RefreshNone;
}

unsigned TrackPanelCell::KeyUp(wxKeyEvent &event, ViewInfo &, wxWindow *)
{
   event.Skip();
   return RefreshCode::RefreshNone;
}

unsigned TrackPanelCell::Char(wxKeyEvent &event, ViewInfo &, wxWindow *)
{
   event.Skip();
   return RefreshCode::RefreshNone;
}

IsVisibleTrack::IsVisibleTrack(AudacityProject *project)
   : mPanelRect {
        wxPoint{ 0, project->mViewInfo.vpos },
        project->GetTPTracksUsableArea()
     }
{}

bool IsVisibleTrack::operator () (const Track *pTrack) const
{
   // Need to return true if this track or a later channel intersects
   // the view
   return
   TrackList::Channels(pTrack).StartingWith(pTrack).any_of(
      [this]( const Track *pT ) {
         wxRect r(0, pT->GetY(), 1, pT->GetHeight());
         return r.Intersects(mPanelRect);
      }
   );
}
