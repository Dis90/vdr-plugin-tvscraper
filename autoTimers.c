#include "autoTimers.h"
#include "tvscraperdb.h"

bool operator< (const cScraperRec &first, int sec) {
  return first.m_movie_tv_id < sec;
}
bool operator< (int first, const cScraperRec &sec) {
  return first < sec.m_movie_tv_id;
}

bool operator< (const cScraperRec &rec1, const cScraperRec &rec2) {
  if (rec1.m_movie_tv_id != rec2.m_movie_tv_id) return rec1.m_movie_tv_id < rec2.m_movie_tv_id;
  if (rec1.m_season_number != rec2.m_season_number) return rec1.m_season_number < rec2.m_season_number;
  return rec1.m_episode_number < rec2.m_episode_number;
}

bool operator< (const cEventMovieOrTv &rec1, const cEventMovieOrTv &rec2) {
  if (rec1.m_movie_tv_id != rec2.m_movie_tv_id) return rec1.m_movie_tv_id < rec2.m_movie_tv_id;
  if (rec1.m_season_number != rec2.m_season_number) return rec1.m_season_number < rec2.m_season_number;
  return rec1.m_episode_number < rec2.m_episode_number;
}

bool operator< (const cScraperRec &first, const cEventMovieOrTv &sec) {
  if (first.m_movie_tv_id != sec.m_movie_tv_id) return first.m_movie_tv_id < sec.m_movie_tv_id;
  if (first.m_season_number != sec.m_season_number) return first.m_season_number < sec.m_season_number;
  return first.m_episode_number < sec.m_episode_number;
}
bool operator< (const cEventMovieOrTv &first, const cScraperRec &sec) {
  if (first.m_movie_tv_id != sec.m_movie_tv_id) return first.m_movie_tv_id < sec.m_movie_tv_id;
  if (first.m_season_number != sec.m_season_number) return first.m_season_number < sec.m_season_number;
  return first.m_episode_number < sec.m_episode_number;
}

bool operator< (const cTimerMovieOrTv &timer1, const cTimerMovieOrTv &timer2) {
  if (timer1.m_movie_tv_id != timer2.m_movie_tv_id) return timer1.m_movie_tv_id < timer2.m_movie_tv_id;
  if (timer1.m_season_number != timer2.m_season_number) return timer1.m_season_number < timer2.m_season_number;
  return timer1.m_episode_number < timer2.m_episode_number;
}

bool operator< (const cTimerMovieOrTv &timer1, const cEventMovieOrTv &sec) {
  if (timer1.m_movie_tv_id != sec.m_movie_tv_id) return timer1.m_movie_tv_id < sec.m_movie_tv_id;
  if (timer1.m_season_number != sec.m_season_number) return timer1.m_season_number < sec.m_season_number;
  return timer1.m_episode_number < sec.m_episode_number;
}

bool operator< (const cEventMovieOrTv &first, const cTimerMovieOrTv &timer2) {
  if (first.m_movie_tv_id != timer2.m_movie_tv_id) return first.m_movie_tv_id < timer2.m_movie_tv_id;
  if (first.m_season_number != timer2.m_season_number) return first.m_season_number < timer2.m_season_number;
  return first.m_episode_number < timer2.m_episode_number;
}


// getEvent ********************************  
const cEvent* getEvent(tEventID eventid, const tChannelID &channelid) {
// note: NULL is returned, if this event is not available
  if ( !channelid.Valid() || eventid == 0 ) return NULL;
  cSchedule const* schedule;
  #if VDRVERSNUM >= 20301
  LOCK_SCHEDULES_READ;
  schedule = Schedules->GetSchedule( channelid );
  #else
  cSchedulesLock schedLock;
  cSchedules const* schedules = cSchedules::Schedules( schedLock );
  schedule = schedules->GetSchedule( channelid );
  #endif
  return schedule->GetEvent( eventid );
}

// createTimer ********************************  
const cTimer* createTimer(const cEvent *event, const char *fileName = NULL) {
  if (!event) return NULL;
  cTimer *timer = new cTimer(event, fileName);
  timer->ClrFlags(tfRecording);
  timer->SetPriority(10);
  timer->SetAux("<tvscraper></tvscraper>");
  LOCK_TIMERS_WRITE;
  Timers->Add(timer);
  return timer;
}

// getCollections ********************************
bool getCollections(const cTVScraperDB &db, const std::set<cScraperRec, std::less<>> &recordings, std::set<int> &collections) {
  for (const cScraperRec &recording : recordings) if (recording.seasonNumber() == -100) {
    int collection_id = db.GetMovieCollectionID(recording.movieTvId() );
    if (collection_id) collections.insert(collection_id);
  }
  return true;
}

// getRecordings ********************************
bool getRecordings(const cTVScraperDB &db, std::set<cScraperRec, std::less<>> &recordings) {
// return true on success, false in case of (temporary) not availability of data
// all recordings where a movie or TV episode was identified are returned.
// In case of duplicates, the better one is returned

  const std::set<std::string> channelsHD = config.GetHD_Channels();
  const std::set<std::string> excludedRecordingFolders = config.GetExcludedRecordingFolders();
#if APIVERSNUM < 20301
  for (cRecording *rec = Recordings.First(); rec; rec = Recordings.Next(rec))
#else
  LOCK_RECORDINGS_READ;
  for (const cRecording *rec = Recordings->First(); rec; rec = Recordings->Next(rec))
#endif
  {
    const char *pos_delim = strrchr(rec->Name(), '~');                                                                                                  
    if (pos_delim != 0)
      if (excludedRecordingFolders.find(std::string(rec->Name(), pos_delim - rec->Name() )) != excludedRecordingFolders.end() ) continue;
    csRecording sRecording(rec);
    tEventID eventID = sRecording.EventID();
    time_t eventStartTime = sRecording.StartTime();
    cString channelIDs = sRecording.ChannelIDs();
    int  movie_tv_id, season_number, episode_number;
    const char sql[] = "select movie_tv_id, season_number, episode_number from recordings2 where event_id = ? and event_start_time = ? and channel_id = ?";
    if (!db.QueryLine(sql, "ils", "iii", (int)eventID, (sqlite3_int64)eventStartTime, (const char *)channelIDs, &movie_tv_id, &season_number, &episode_number)) continue;
    if (movie_tv_id == 0) continue;
    if (season_number == 0 && episode_number == 0) continue; // we look only for recordings we can assign to a specific episode/movie
    if (season_number == -100) episode_number = 0;
    bool hd = channelsHD.find((const char *)channelIDs) != channelsHD.end();
#if VDRVERSNUM >= 20505
    int numberOfErrors = rec->Info()->Errors();
#else
    int numberOfErrors = 0;
#endif
    cScraperRec scraperRec(eventID, eventStartTime, (const char *)channelIDs, rec->Name(), movie_tv_id, season_number, episode_number, hd, numberOfErrors);
    auto found = recordings.find(scraperRec);
    if (found == recordings.end() ) recordings.insert(std::move(scraperRec)); // not in list -> insert
    else if (scraperRec.isBetter(*found)) {
      recordings.erase(found);
      recordings.insert(std::move(scraperRec));
    }
  }
// don't remove entries without action item, return all (unique) recordings
  return true;
}

bool InsertIfBetter (std::set<cTimerMovieOrTv, std::less<>> &timers, const cTimerMovieOrTv &timer, cTimerMovieOrTv *obsoletTimer = NULL) {
// in case timer is already in list, insert "better" timer in list, and return "worse" timer in obsoletTimer if requested
// return true if timer is already in list
  auto found = timers.find(timer);
  if (found == timers.end() ) {
    timers.insert(timer);
    return false;
  }
  if (timer.isBetter(*found) ) {
    if (obsoletTimer) *obsoletTimer = *found;
    timers.erase(found);
    timers.insert(timer);
    return true;
  }
  if (obsoletTimer) *obsoletTimer = timer;
  return true;
}
// getAllTimers  ********************************
void getAllTimers (const cTVScraperDB &db, std::set<cTimerMovieOrTv, std::less<>> &otherTimers, std::set<cTimerMovieOrTv, std::less<>> &myTimers) {
// only timers where a movie or TV episode was found (i.a. only events in db, and only if episode was found)
// for each movie or tv, only one (the best) timer. HD better than SD. Otherwise, the earlier the better
  std::vector<int> timersToDelete;
  cTimerMovieOrTv timer;
  cTimerMovieOrTv obsoletTimer;
  { // start LOCK_TIMERS_READ block
#if VDRVERSNUM >= 20301
  LOCK_TIMERS_READ;
  for (const cTimer *ti = Timers->First(); ti; ti = Timers->Next(ti)) if (ti->Local())
#else
  for (const cTimer *ti = Timers.First(); ti; ti = Timers.Next(ti))
#endif
  {
    if (!ti->Event() ) continue; // own timers without event are deleted in AdjustSpawnedScraperTimers
// get movie/tv for event
    cMovieOrTv *movieOrTv = NULL;
    csEventOrRecording *sEventOrRecording = GetsEventOrRecording(ti->Event(), NULL);
    if (sEventOrRecording) {
      movieOrTv = cMovieOrTv::getMovieOrTv(&db, sEventOrRecording);
      delete sEventOrRecording;
    }
    bool mIdentified = movieOrTv && (movieOrTv->getType() == tMovie || (movieOrTv->getSeason() != 0 || movieOrTv->getEpisode() != 0));
    bool ownTimer = ti->Aux() && strncmp(ti->Aux(), "<tvscraper>", 11) == 0;
    if (!mIdentified) {
      if (ownTimer) timersToDelete.push_back(ti->Id());
      if (movieOrTv) delete movieOrTv;
      continue;
    }
    timer.m_timerId = ti->Id();
    timer.m_tstart = ti->StartTime();
    timer.m_needed = false;
    timer.m_movie_tv_id = movieOrTv->dbID();
    if (movieOrTv->getType() == tMovie) {
      timer.m_season_number = -100;
      timer.m_episode_number = 0;
    } else {
      timer.m_season_number = movieOrTv->getSeason();
      timer.m_episode_number = movieOrTv->getEpisode();
    }
    delete movieOrTv;
    timer.m_hd = config.GetHD_Channels().find(*(ti->Channel()->GetChannelID().ToString())) == config.GetHD_Channels().end()?0:1;
    if (ownTimer) {
// my timer
      if (InsertIfBetter(myTimers, timer, &obsoletTimer)) timersToDelete.push_back(obsoletTimer.m_timerId);
    } else {
// not my timer
      InsertIfBetter(otherTimers, timer);
    }
  }
  } // end LOCK_TIMERS_READ block
// delete obsolete timers
  for (const int &timerToDelete: timersToDelete) {
#if VDRVERSNUM >= 20301
    LOCK_TIMERS_WRITE;
    cTimer *ti = Timers->GetById(timerToDelete);
    if (ti) Timers->Del(ti);
#else
    cTimer *ti = Timers.GetById(timerToDelete);
    if (ti) Timers.Del(ti);
#endif
  }
  return;
}

// getAllEvents ********************************
std::set<cEventMovieOrTv> getAllEvents(const cTVScraperDB &db) {
// ignore events which are running or about to start (in 10 mins or less)
// only events where a movie or TV episode was found (i.a. only events in db, and only if episode was found)
// for each movie or tv, only one (the best) event. HD better than SD. Otherwise, the earlier the better
  std::set<cEventMovieOrTv> result;
  const time_t now_m10 = time(0) - 10*60;
  const char sql_event[] = "select event_id, channel_id, valid_till, movie_tv_id, season_number, episode_number from event";
  cEventMovieOrTv scraperEvent;
  int l_event_id;
  char *l_channel_id;
  sqlite3_int64 l_validTill;
  for (sqlite3_stmt *statement = db.QueryPrepare(sql_event, "");
    db.QueryStep(statement, "isliii", &l_event_id, &l_channel_id, &l_validTill, &scraperEvent.m_movie_tv_id, &scraperEvent.m_season_number, &scraperEvent.m_episode_number);)
  {
    if (scraperEvent.m_season_number == 0 && scraperEvent.m_episode_number == 0) continue;
    scraperEvent.m_event = getEvent(l_event_id, tChannelID::FromString(l_channel_id));
    if (!scraperEvent.m_event) continue;
    if (scraperEvent.m_season_number == -100) scraperEvent.m_episode_number = 0;
    if (scraperEvent.m_event->IsRunning(true) ) continue;  // event already started, ignore
    if (scraperEvent.m_event->StartTime() <= now_m10 ) continue;  // IsRunning does sometimes not work
    scraperEvent.m_hd = config.GetHD_Channels().find(l_channel_id) == config.GetHD_Channels().end()?0:1;
    auto found = result.find(scraperEvent);
    if (found == result.end() )
      result.insert(scraperEvent);
    else if (scraperEvent.isBetter(*found)) {
      result.erase(found);
      result.insert(std::move(scraperEvent));
    }
  }
  return result;
}

void createTimer(const cTVScraperDB &db, const cEventMovieOrTv &scraperEvent, const cScraperRec &recording) {
// create timer for event, in same folder as existing recording
  size_t pos = recording.name().find_last_of('~');
  if (pos == std::string::npos || pos == 0) {
    createTimer(scraperEvent.m_event);
    return;
  }
  if (recording.seasonNumber() == -100) {
    createTimer(scraperEvent.m_event, (recording.name().substr(0, pos + 1) + scraperEvent.m_event->Title()).c_str() );
    return;
  }
// TV show. Test: Title~ShortText ?
  size_t pos2 = recording.name().find_last_of('~', pos - 1);
  if (pos2 == std::string::npos) pos2 = 0;
  else pos2++;
  if (recording.name().substr(pos2, pos-pos2).compare(scraperEvent.m_event->Title() ) == 0) {
// structure of old recording: title/episode_name
    if (scraperEvent.m_event->ShortText() && *scraperEvent.m_event->ShortText() )
      createTimer(scraperEvent.m_event, (recording.name().substr(0, pos + 1) + scraperEvent.m_event->ShortText()).c_str() );
    else {
      const char sql[] = "select episode_name from tv_s_e where tv_id = ? and season_number = ? and episode_number = ?";
      std::string episode_name = db.QueryString(sql, "iii", scraperEvent.m_movie_tv_id, scraperEvent.m_season_number, scraperEvent.m_episode_number);
      if (episode_name.empty() && scraperEvent.m_event->Description() && *scraperEvent.m_event->Description() )
        episode_name = strlen(scraperEvent.m_event->Description()) < 50?scraperEvent.m_event->Description():std::string(scraperEvent.m_event->Description(), 50);
      if (episode_name.empty() ) episode_name = "no episode name found";
      createTimer(scraperEvent.m_event, (recording.name().substr(0, pos + 1) + episode_name).c_str() );
    }
  } else // structure of old recording: title
    createTimer(scraperEvent.m_event, (recording.name().substr(0, pos + 1) + scraperEvent.m_event->Title()).c_str() );
  return;
}

bool AdjustSpawnedTimer(cTimer *ti)
{
  if (ti->Event()) {
    if (ti->Event()->Schedule()) { // events may be deleted from their schedule in cSchedule::DropOutdated()!
      // Adjust the timer to shifted start/stop times of the event if necessary:
      time_t tstart = ti->Event()->StartTime();
      time_t tstop = ti->Event()->EndTime();
      int MarginStart = 0;
      int MarginStop  = 0;
      ti->CalcMargins(MarginStart, MarginStop, ti->Event());
      tstart -= MarginStart;
      tstop  += MarginStop;
      // Event start/end times are given in "seconds since the epoch". Some broadcasters use values
      // that result in full minutes (with zero seconds), while others use any values. VDR's timers
      // use times given in full minutes, truncating any seconds. Thus we only react if the start/stop
      // times of the timer are off by at least one minute:
      if (abs(ti->StartTime() - tstart) >= 60 || abs(ti->StopTime() - tstop) >= 60) {
        cString OldDescr = ti->ToDescr();
        struct tm tm_r;
        struct tm *time = localtime_r(&tstart, &tm_r);
        ti->SetDay(cTimer::SetTime(tstart, 0));
        ti->SetStart(time->tm_hour * 100 + time->tm_min);
        time = localtime_r(&tstop, &tm_r);
        ti->SetStop(time->tm_hour * 100 + time->tm_min);
        ti->Matches();
        isyslog("tvscraper: timer %s times changed to %s-%s", *OldDescr, *TimeString(tstart), *TimeString(tstop));
        return true;
      }
    }
  }
  return false;
}

bool AdjustSpawnedScraperTimers() {
  bool TimersModified = false;
  cTimer *ti_del = NULL;
#if VDRVERSNUM >= 20301
  LOCK_TIMERS_WRITE;
  for (cTimer *ti = Timers->First(); ti; ti = Timers->Next(ti)) if (ti->Local())
#else
  for (cTimer *ti = Timers.First(); ti; ti = Timers.Next(ti))
#endif
  {
    if (ti_del) { Timers->Del(ti_del); ti_del = NULL; }
    if (ti->Aux() && strncmp(ti->Aux(), "<tvscraper>", 11) == 0) {
      if (!ti->Event() || ! ti->Event()->Schedule() ) ti_del = ti;
      else if (!ti->HasFlags(tfVps))
        TimersModified |= AdjustSpawnedTimer(ti);
    }
  }
  if (ti_del) { Timers->Del(ti_del); ti_del = NULL; }
  return TimersModified;
}

bool checkTimer(const cEventMovieOrTv &scraperEvent, std::set<cTimerMovieOrTv, std::less<>> &myTimers) {
// return true if it is required to create a new timer
  std::set<cTimerMovieOrTv, std::less<>>::iterator found = myTimers.find(scraperEvent);
  if (found != myTimers.end() && scraperEvent.m_hd <= found->m_hd ) {
    (*found).m_needed = true;
    return false;
  }
  return true;
}

// timerForEvent ********************************
bool timerForEvent(const cTVScraperDB &db, const cEventMovieOrTv &scraperEvent, std::set<cTimerMovieOrTv, std::less<>> &myTimers, const std::set<cScraperRec, std::less<>> &recordings, const std::set<int> &collections) {
// check:
//   is there a recording, which can be improved -> timer
//   else:
//      is event part of a collection, that shall be recorded -> timer
//      is event part of a TV show, that shall be recorded -> timer
// return false if no timer is available or created. true, otherwise

// is there a recording?
  auto found = recordings.find(scraperEvent);
  if (found != recordings.end()) {
    if (scraperEvent.m_hd <  found->hd() ) return false;
    if (scraperEvent.m_hd == found->hd() && found->numberOfErrors() == 0) return false;
// we need a timer ...
    if (checkTimer(scraperEvent, myTimers)) createTimer(db, scraperEvent, *found);
    return true;
  }
// there is no recording with this movie / TV show
  if (scraperEvent.m_season_number == -100) {
// movie. Check: record because of collection?
    int collection_id = db.GetMovieCollectionID(scraperEvent.m_movie_tv_id);
    if (collection_id <= 0) return false;
    if (collections.find(collection_id)  == collections.end() ) return false;
// find recording with this collection (so new recording will be in same folder)
    char sql[] = "select movie_id from movies3 where movie_collection_id = ?";
    cEventMovieOrTv eventMovieOrTv;
    eventMovieOrTv.m_season_number = -100;
    eventMovieOrTv.m_episode_number = 0;
    for (sqlite3_stmt *statement = db.QueryPrepare(sql, "i", collection_id);
          db.QueryStep(statement, "i", &eventMovieOrTv.m_movie_tv_id);) {
      auto found_c = recordings.find(eventMovieOrTv);
      if (found_c == recordings.end() ) continue;
      if (checkTimer(scraperEvent, myTimers)) createTimer(db, scraperEvent, *found_c);
      return true;
    }
// no recording with this collection found (?)
    if (checkTimer(scraperEvent, myTimers)) createTimer(scraperEvent.m_event);
    return true;
  }
// TV show. Check: record because of all episodes shall be recorded?
  if (config.GetTV_Shows().find(scraperEvent.m_movie_tv_id) == config.GetTV_Shows().end() ) return false;
// find recording with this TV show, so same folder can be used
  auto found_s = recordings.find(scraperEvent.m_movie_tv_id);
  if (found_s == recordings.end() ) {
    if (checkTimer(scraperEvent, myTimers)) createTimer(scraperEvent.m_event);
    return true;
  }
  if (checkTimer(scraperEvent, myTimers)) createTimer(db, scraperEvent, *found_s);
  return true;
}


bool timersForEvents(const cTVScraperDB &db) {
  std::set<cScraperRec, std::less<>> recordings;
  if (!getRecordings(db, recordings) ) return false;
  std::set<int> collections;
  if (!getCollections(db, recordings, collections) ) return false;

  AdjustSpawnedScraperTimers();
  std::set<cTimerMovieOrTv, std::less<>> otherTimers;
  std::set<cTimerMovieOrTv, std::less<>> myTimers;
  getAllTimers(db, otherTimers, myTimers);

  for (const cEventMovieOrTv &scraperEvent: getAllEvents(db) ) {
    auto found = otherTimers.find(scraperEvent);
    if (found != otherTimers.end() && scraperEvent.m_hd <= found->m_hd ) continue;
    timerForEvent(db, scraperEvent, myTimers, recordings, collections);
  }
// delete obsolete timers
  for (const cTimerMovieOrTv &timerToDelete: myTimers) if (!timerToDelete.m_needed) {
#if VDRVERSNUM >= 20301
    LOCK_TIMERS_WRITE;
    cTimer *ti = Timers->GetById(timerToDelete.m_timerId);
    if (ti) Timers->Del(ti);
#else
    cTimer *ti = Timers.GetById(timerToDelete.m_timerId);
    if (ti) Timers.Del(ti);
#endif
  }
  return true;
}
