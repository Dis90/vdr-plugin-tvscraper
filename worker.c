﻿#include <locale.h>
#include <vdr/menu.h>
#include "worker.h"

using namespace std;

cTVScraperWorker::cTVScraperWorker(cTVScraperDB *db, cOverRides *overrides) : cThread("tvscraper", true) {
    startLoop = true;
    scanVideoDir = false;
    manualScan = false;
    this->db = db;
    this->overrides = overrides;
    moviedbScraper = NULL;
    tvdbScraper = NULL;
//    initSleep = 2 * 60 * 1000;
    initSleep =     60 * 1000;
    loopSleep = 5 * 60 * 1000;
}

cTVScraperWorker::~cTVScraperWorker() {
    if (moviedbScraper)
        delete moviedbScraper;
    if (tvdbScraper)
        delete tvdbScraper;
}

void cTVScraperWorker::Stop(void) {
    waitCondition.Broadcast();    // wakeup the thread
    Cancel(210);                    // wait up to 210 seconds for thread was stopping
//    db->BackupToDisc();
}

void cTVScraperWorker::InitVideoDirScan(void) {
    scanVideoDir = true;
    waitCondition.Broadcast();
}

void cTVScraperWorker::InitManualScan(void) {
    manualScan = true;
    waitCondition.Broadcast();
}

void cTVScraperWorker::SetDirectories(void) {
    if (config.GetBaseDirLen() == 0) {
      esyslog("tvscraper: ERROR: no base dir");
      startLoop = false;
      return;
    }
    plgBaseDir = config.GetBaseDir();
    stringstream strSeriesDir;
    strSeriesDir << plgBaseDir << "series";
    seriesDir = strSeriesDir.str();
    stringstream strMovieDir;
    strMovieDir << plgBaseDir << "movies";
    movieDir = strMovieDir.str();
    bool ok = false;
    ok = CreateDirectory(plgBaseDir.substr(0, config.GetBaseDirLen()-1 ));
    if (ok) ok = CreateDirectory(config.GetBaseDirEpg() );
    if (ok)
        ok = CreateDirectory(seriesDir);
    if (ok)
        ok = CreateDirectory(movieDir);
    if (ok)
        ok = CreateDirectory(movieDir + "/tv");
    if (!ok) {
        esyslog("tvscraper: ERROR: check %s for write permissions", plgBaseDir.c_str());
        startLoop = false;
    } else {
        dsyslog("tvscraper: using base directory %s", plgBaseDir.c_str());
    }
}

bool cTVScraperWorker::ConnectScrapers(void) {
  if (!moviedbScraper) {
    moviedbScraper = new cMovieDBScraper(movieDir, db, overrides);
    if (!moviedbScraper->Connect()) {
	esyslog("tvscraper: ERROR, connection to TheMovieDB failed");
	delete moviedbScraper;
	moviedbScraper = NULL;
	return false;
    }
  }
  if (!tvdbScraper) {
    tvdbScraper = new cTVDBScraper(seriesDir, db);
    if (!tvdbScraper->Connect()) {
	esyslog("tvscraper: ERROR, connection to TheTVDB failed");
	delete tvdbScraper;
	tvdbScraper = NULL;
	return false;
    }
  }
  return true;
}

vector<tEventID> GetEventIDs(std::string &channelName, const tChannelID &channelid, int &msg_cnt) {
// Return a list of event IDs (in vector<tEventID> eventIDs)
vector<tEventID> eventIDs;
#if APIVERSNUM < 20301
  const cChannel *channel = Channels.GetByChannelID(channelid);
#else
  LOCK_CHANNELS_READ;
  const cChannel *channel = Channels->GetByChannelID(channelid);
#endif
  if (!channel) {
    msg_cnt++;
    if (msg_cnt < 5) dsyslog("tvscraper: Channel %s is not availible, skipping. Most likely this channel does not exist. To get rid of this message, goto tvscraper settings and edit the channel list.", (const char *)channelid.ToString());
    if (msg_cnt == 5) dsyslog("tvscraper: Skipping further messages: Channel %s is not availible, skipping. Most likely this channel does not exist. To get rid of this message, goto tvscraper settings and edit the channel list.", (const char *)channelid.ToString());
    return eventIDs;
  }
  channelName = channel->Name();
// now get and lock the schedule
#if APIVERSNUM < 20301
  cSchedulesLock schedulesLock;
  const cSchedules *Schedules = cSchedules::Schedules(schedulesLock);
#else
  LOCK_SCHEDULES_READ;
#endif
  const cSchedule *Schedule = Schedules->GetSchedule(channel);
  if (Schedule) {
    for (const cEvent *event = Schedule->Events()->First(); event; event = Schedule->Events()->Next(event))
      if (event->EndTime() >= time(0) ) eventIDs.push_back(event->EventID());
  } else dsyslog("tvscraper: Schedule for channel %s %s is not availible, skipping", channel->Name(), (const char *)channelid.ToString() );
  return eventIDs;
}


bool cTVScraperWorker::ScrapEPG(void) {
// true if one or more new events were scraped
  bool newEvent = false;
  if (config.GetReadOnlyClient() ) return newEvent;
// check for changes in schedule (works only if APIVERSNUM >= 20301
#if APIVERSNUM >= 20301
  if (!cSchedules::GetSchedulesRead(schedulesStateKey)) {
    if (config.enableDebug) dsyslog("tvscraper: Schedule was not changed, skipping scan");
    return newEvent;
  }
  schedulesStateKey.Remove();
#endif
// loop over all channels configured for scraping, and create map to enable check for new events
  map<tChannelID, set<int>*> currentEvents;
  set<tChannelID> channels;
  int msg_cnt = 0;
  int i_channel_num = 0;
  for (const tChannelID &channelID: config.GetScrapeChannels() ) {  // GetScrapeChannels creates a copy, thread save
    if (i_channel_num++ > 1000) {
      esyslog("tvscraper: ERROR: don't scrape more than 1000 channels");
      break;
    }
    cTvspEpg tvspEpg(config.GetChannelMapEpg(channelID));

    map<tChannelID, set<int>*>::iterator currentEventsCurrentChannelIT = currentEvents.find(channelID);
    if (currentEventsCurrentChannelIT == currentEvents.end() ) {
      currentEvents.insert(pair<tChannelID, set<int>*>(channelID, new set<int>));
      currentEventsCurrentChannelIT = currentEvents.find(channelID);
    }
    map<tChannelID, set<int>*>::iterator lastEventsCurrentChannelIT = lastEvents.find(channelID);
    bool newEventSchedule = false;
    std::string channelName;
    vector<tEventID> eventIDs = GetEventIDs(channelName, channelID, msg_cnt);
    for (const tEventID &eventID: eventIDs) {
      if (!Running()) {
        for (auto &event: currentEvents) delete event.second;
        return newEvent;
      }
// insert this event in the list of scraped events
      currentEventsCurrentChannelIT->second->insert(eventID);
// check: was this event scraped already?
      if (lastEventsCurrentChannelIT == lastEvents.end() ||
          lastEventsCurrentChannelIT->second->find(eventID) == lastEventsCurrentChannelIT->second->end() ) {
// this event was not yet scraped, scrape it now
        cMovieOrTv *movieOrTv = NULL;
        { // start locks
#if VDRVERSNUM >= 20301
          LOCK_SCHEDULES_READ;
#endif
          cEvent *event = getEvent(eventID, channelID);
          if (!event) continue;
          newEvent = true;
          if (!newEventSchedule) {
            dsyslog("tvscraper: scraping Channel %s %s", channelName.c_str(), (const char *)channelID.ToString());
            newEventSchedule = true;
          }
					tvspEpg.enhanceEvent(event);
          csEventOrRecording sEvent(event);
          cSearchEventOrRec SearchEventOrRec(&sEvent, overrides, moviedbScraper, tvdbScraper, db);
          movieOrTv = SearchEventOrRec.Scrape();
        } // end of locks
        if (movieOrTv) {
          movieOrTv->DownloadImages(moviedbScraper, tvdbScraper, "");
          delete movieOrTv;
          movieOrTv = NULL;
        }
        waitCondition.TimedWait(mutex, 10); // short wait time after scraping an event
      }
    }
  } // end loop over all channels
  currentEvents.swap(lastEvents);
  for (auto &event: currentEvents) delete event.second;
  return newEvent;
}

void cTVScraperWorker::ScrapRecordings(void) {
  if (config.GetReadOnlyClient() ) return;
  db->ClearRecordings2();

  cMovieOrTv *movieOrTv = NULL;
#if APIVERSNUM < 20301
  for (cRecording *rec = Recordings.First(); rec; rec = Recordings.Next(rec)) {
    if (overrides->IgnorePath(rec->FileName())) continue;
    {
#else
  vector<string> recordingFileNames;
  {
    LOCK_RECORDINGS_READ;
    for (const cRecording *rec = Recordings->First(); rec; rec = Recordings->Next(rec)) {
      if (overrides->IgnorePath(rec->FileName())) continue;
      if (rec->FileName()) recordingFileNames.push_back(rec->FileName());
    }
  }
  for (const string &filename: recordingFileNames) {
    {
      LOCK_RECORDINGS_READ;
      const cRecording *rec = Recordings->GetByName(filename.c_str() );
      if (!rec) continue;
#endif
      if (config.enableDebug) esyslog("tvscraper: Scrap recording \"%s\"", rec->FileName() );

      csRecording csRecording(rec);
      const cRecordingInfo *recInfo = rec->Info();
      const cEvent *recEvent = recInfo->GetEvent();

      if (recEvent) {
          cSearchEventOrRec SearchEventOrRec(&csRecording, overrides, moviedbScraper, tvdbScraper, db);  
          movieOrTv = SearchEventOrRec.Scrape();
      }
    }
// here, the read lock is released, so wait a short time, in case someone needs a write lock
    if (movieOrTv) {
      movieOrTv->DownloadImages(moviedbScraper, tvdbScraper, filename);
      delete movieOrTv;
      movieOrTv = NULL;
    }
    if (!Running() ) break;
    waitCondition.TimedWait(mutex, 100);
  }
}

bool cTVScraperWorker::TimersRunningPlanned(double nextMinutes) {
// return true is a timer is running, or a timer will start within the next nextMinutes minutes
// otherwise false
  if (config.GetReadOnlyClient() ) return true;
#if APIVERSNUM < 20301
  for (cTimer *timer = Timers.First(); timer; timer = Timers.Next(timer)) {
#else
  LOCK_TIMERS_READ;
  for (const cTimer *timer = Timers->First(); timer; timer = Timers->Next(timer)) if (timer->Local() ) {
#endif
    if (timer->Recording()) return true;
    if (timer->HasFlags(tfActive) && (difftime(timer->StartTime(), time(0) )*60 < nextMinutes)) return true;
  }
  return false;
}

void writeTimerInfo(const cTimer *timer, const char *pathName) {
  std::string filename = concatenate(pathName, "/tvscrapper.json");

  rapidjson::Document document;
  if (jsonReadFile(document, filename.c_str())) return; // error parsing json file
  if (document.HasMember("timer") ) return;  // timer information already available

  rapidjson::Value timer_j;
  timer_j.SetObject();

  timer_j.AddMember("vps", rapidjson::Value().SetBool(timer->HasFlags(tfVps)), document.GetAllocator());
  timer_j.AddMember("start_time", rapidjson::Value().SetInt(timer->StartTime()), document.GetAllocator());
  timer_j.AddMember("stop_time", rapidjson::Value().SetInt(timer->StopTime()), document.GetAllocator());

  document.AddMember("timer", timer_j, document.GetAllocator());

  jsonWriteFile(document, filename.c_str());
}

bool cTVScraperWorker::CheckRunningTimers(void) {
// assign scrape result from EPG to recording
// return true if new data are assigned to one or more recordings
  if (config.GetReadOnlyClient() ) return false;
  bool newRecData = false;
  vector<string> recordingFileNames; // filenames of recordings with running timers
  { // in this block, we lock the timers
#if APIVERSNUM < 20301
    for (cTimer *timer = Timers.First(); timer; timer = Timers.Next(timer))
#else
    LOCK_TIMERS_READ;
    for (const cTimer *timer = Timers->First(); timer; timer = Timers->Next(timer)) if (timer->Local() )
#endif
    if (timer->Recording()) {
// figure out recording
      cRecordControl *rc = cRecordControls::GetRecordControl(timer);
      if (!rc) {
        esyslog("tvscraper: ERROR cTVScraperWorker::CheckRunningTimers: Timer is recording, but there is no cRecordControls::GetRecordControl(timer)");
        continue;
      }
      recordingFileNames.push_back(rc->FileName() );
      writeTimerInfo(timer, rc->FileName() );
    }
  } // timer lock is released
  for (const string &filename: recordingFileNames) {
// loop over all recordings with running timers
    cMovieOrTv *movieOrTv = NULL;
    { // in this block we have recording locks
#if APIVERSNUM < 20301
      const cRecording *recording = Recordings.GetByName(filename.c_str() );
#else
      LOCK_RECORDINGS_READ;
      const cRecording *recording = Recordings->GetByName(filename.c_str() );
#endif
      if (!recording) {
        esyslog("tvscraper: ERROR cTVScraperWorker::CheckRunningTimers: no recording for file \"%s\"", filename.c_str() );
        continue;
      }
      csRecording sRecording(recording);
      int r = db->SetRecording(&sRecording);
      if (r == 2) {
        newRecData = true;
        movieOrTv = cMovieOrTv::getMovieOrTv(db, &sRecording);
        if (movieOrTv) {
          if (!movieOrTv->copyImagesToRecordingFolder(recording->FileName() )) {
            if (recording->Info()) CopyFile(getEpgImagePath(recording->Info()->GetEvent(), false), std::string(recording->FileName() ) + "/fanart.jpg");
          }
          delete movieOrTv;
          movieOrTv = NULL;
        } else
          if (recording->Info()) CopyFile(getEpgImagePath(recording->Info()->GetEvent(), false), std::string(recording->FileName() ) + "/fanart.jpg");
      }
      if (r == 0) {
        tEventID eventID = sRecording.EventID();
        cString channelIDs = sRecording.ChannelIDs();
        esyslog("tvscraper: cTVScraperWorker::CheckRunningTimers: no entry in table event found for eventID %i, channelIDs %s, recording for file \"%s\"", eventID, (const char *)channelIDs, filename.c_str() );
        if (ConnectScrapers() ) { 
          cSearchEventOrRec SearchEventOrRec(&sRecording, overrides, moviedbScraper, tvdbScraper, db);  
          movieOrTv = SearchEventOrRec.Scrape();
          if (movieOrTv) newRecData = true;
        }
      }
    } // the locks are released
    if (movieOrTv) {
      movieOrTv->DownloadImages(moviedbScraper, tvdbScraper, filename);
      delete movieOrTv;
      movieOrTv = NULL;
    }
  }
  if (newRecData && !recordingFileNames.empty() ) TouchFile(config.GetRecordingsUpdateFileName().c_str());
  return newRecData;
}

bool cTVScraperWorker::StartScrapping(bool &fullScan) {
  fullScan = false;
  if (!manualScan && TimersRunningPlanned(15.) ) return false;
  bool resetScrapeTime = false;
  if (manualScan) {
    manualScan = false;
    for (auto &event: lastEvents) event.second->clear();
    resetScrapeTime = true;
  }
  if (resetScrapeTime || lastEvents.empty() ) {
// a full scrape will be done, write this to db, so next full scrape will be in one day
    db->execSql("delete from scrap_checker", "");
    char sql[] = "INSERT INTO scrap_checker (last_scrapped) VALUES (?)";
    db->execSql(sql, "t", time(0));
    fullScan = true;
    return true;
  }
// Delete the scraped event IDs once a day, to update data
  int minTime = 24 * 60 * 60;
  if (db->CheckStartScrapping(minTime)) {
    for (auto &event: lastEvents) event.second->clear();
    fullScan = true;
  }
  return true;
}

void cTVScraperWorker::Action(void) {
  if (!startLoop) return;
  if (config.GetReadOnlyClient() ) return;

  mutex.Lock();
  dsyslog("tvscraper: waiting %d minutes to start main loop", initSleep / 1000 / 60);
  waitCondition.TimedWait(mutex, initSleep);

  while (Running()) {
    if (scanVideoDir) {
      scanVideoDir = false;
      dsyslog("tvscraper: scanning video dir");
      if (ConnectScrapers()) {
        ScrapRecordings();
        dsyslog("tvscraper: touch \"%s\"", config.GetRecordingsUpdateFileName().c_str());
        TouchFile(config.GetRecordingsUpdateFileName().c_str());
//      db->BackupToDisc();
      }
      dsyslog("tvscraper: scanning video dir done");
      continue;
    }
    bool newRec = CheckRunningTimers();
    if (!Running() ) break;
    if (newRec) db->BackupToDisc();
    if (!Running() ) break;
    bool fullScan = false;
    if (StartScrapping(fullScan)) {
      dsyslog("tvscraper: start scraping epg");
      if (ConnectScrapers()) {
        bool newEvents = ScrapEPG();
        if (newEvents) TouchFile(config.GetEPG_UpdateFileName().c_str());
        if (newEvents && Running() ) cMovieOrTv::DeleteAllIfUnused(db);
        if (fullScan && Running()) db->BackupToDisc();
      }
      dsyslog("tvscraper: epg scraping done");
      if (!Running() ) break;
      if (config.getEnableAutoTimers() ) timersForEvents(*db);
    }
    waitCondition.TimedWait(mutex, loopSleep);
  }
}
