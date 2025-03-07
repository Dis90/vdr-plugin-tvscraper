#include "services.h"
class cCharacterImp: public cCharacter {
  public:
    cCharacterImp(eCharacterType type, const std::string &personName, const std::string &characterName = "", const cTvMedia &image = cTvMedia() ):
      cCharacter(),
      m_type(type),
      m_personName(personName),
      m_characterName(characterName),
      m_image(image) {}
    virtual eCharacterType getType() { return m_type; }
    virtual const std::string &getPersonName() { return m_personName; }
    virtual const std::string &getCharacterName() { return m_characterName; }
    virtual const cTvMedia &getImage() { return m_image; }
    virtual ~cCharacterImp() {}
  private:
    const eCharacterType m_type;
    const std::string m_personName;     // "real name" of the person
    const std::string m_characterName;  // name of character in video
    const cTvMedia m_image;
};

class cScraperVideoImp: public cScraperVideo {
  public:
    cScraperVideoImp(const cEvent *event, const cRecording *recording, cTVScraperDB *db);

// with the following methods you can request the "IDs of the identified object"
    virtual tvType getVideoType(); // if this is tNone, nothing was identified by scraper. Still, some information (image, duration deviation ...) might be available
    virtual int getDbId();
    virtual int getEpisodeNumber();
    virtual int getSeasonNumber();

    virtual bool getOverview(std::string *title, std::string *episodeName, std::string *releaseDate, int *runtime, std::string *imdbId, int *collectionId, std::string *collectionName); // return false if no scraper data are available

    virtual cTvMedia getImage(cImageLevels imageLevels = cImageLevels(), cOrientations imageOrientations = cOrientations(), bool fullPath = true);
    virtual std::vector<cTvMedia> getImages(eOrientation orientation, int maxImages = 3, bool fullPath = true);
    virtual std::vector<std::unique_ptr<cCharacter>> getCharacters(bool fullPath = true);
    virtual int getDurationDeviation();
    virtual int getHD();  // 0: SD. 1: HD. 2: UHD (note: might be changed in future. But: the higher, the better)
    virtual int getLanguage();
    virtual bool getMovieOrTv(std::string *title, std::string *originalTitle, std::string *tagline, std::string *overview, std::vector<std::string> *genres, std::string *homepage, std::string *releaseDate, bool *adult, int *runtime, float *popularity, float *voteAverage, int *voteCount, std::vector<std::string> *productionCountries, std::string *imdbId, int *budget, int *revenue, int *collectionId, std::string *collectionName, std::string *status, std::vector<std::string> *networks, int *lastSeason);

// episode specific ======================================
    virtual bool getEpisode(std::string *name, std::string *overview, int *absoluteNumber, std::string *firstAired, int *runtime, float *voteAverage, int *voteCount, std::string *imdbId);
    virtual ~cScraperVideoImp();
  private:
    const cEvent *m_event = NULL;
    const cRecording *m_recording = NULL;
    cTVScraperDB *m_db;
    int m_runtime_guess = 0; // runtime of thetvdb, which does best match to runtime of recording. Ignore if <=0
    int m_runtime = -2;  // runtime of episode / movie. Most reliable. -2: not checked. -1, 0: checked, but not available
    int m_collectionId = -2; // collection ID. -2: not checked
    csEventOrRecording *m_sEventOrRecording = NULL;
    cMovieOrTv *m_movieOrTv = NULL;
    bool isEpisodeIdentified();
};

cScraperVideoImp::cScraperVideoImp(const cEvent *event, const cRecording *recording, cTVScraperDB *db):cScraperVideo() {
  if (event && recording) {
    esyslog("tvscraper: ERROR calling vdr service interface, call->event && call->recording are provided. Please set one of these parameters to NULL");
    return;
  }
  m_event = event;
  m_recording = recording;
  m_db = db;
  m_sEventOrRecording = GetsEventOrRecording(event, recording);
  if (!m_sEventOrRecording) return;
  m_movieOrTv = cMovieOrTv::getMovieOrTv(db, m_sEventOrRecording, &m_runtime_guess);
}
cScraperVideoImp::~cScraperVideoImp() {
  if (m_sEventOrRecording) delete m_sEventOrRecording;
  if (m_movieOrTv) delete m_movieOrTv;
}

tvType cScraperVideoImp::getVideoType() {
// if this is tNone, nothing was identified by scraper. Still, some information (image, duration deviation ...) might be available
  if (!m_movieOrTv) return tNone;
  return m_movieOrTv->getType();
}
int cScraperVideoImp::getDbId() {
  if (!m_movieOrTv) return 0;
  return m_movieOrTv->dbID();
}

bool cScraperVideoImp::getOverview(std::string *title, std::string *episodeName, std::string *releaseDate, int *runtime, std::string *imdbId, int *collectionId, std::string *collectionName) {
  m_runtime = 0;
  m_collectionId = 0;
  bool scraperDataAvaiable = false;
  if (m_movieOrTv) scraperDataAvaiable = m_movieOrTv->getOverview(title, episodeName, releaseDate, &m_runtime, imdbId, &m_collectionId, collectionName);
  if (collectionId) *collectionId = m_collectionId;
  if (runtime) *runtime = m_runtime > 0?m_runtime:m_runtime_guess;
  if (!scraperDataAvaiable) return false;
  return true;
}

int cScraperVideoImp::getDurationDeviation() {
  if (!m_sEventOrRecording) return 0;
  if (m_runtime == -2 && m_movieOrTv) {
    m_runtime = 0;
    m_collectionId = 0;
    m_movieOrTv->getOverview(NULL, NULL, NULL, &m_runtime, NULL, &m_collectionId);
  }
// note: m_runtime <=0 is considered as no runtime information by m_sEventOrRecording->durationDeviation(m_runtime)
  return m_sEventOrRecording->durationDeviation(m_runtime);
}

bool orientationsIncludesLandscape(cOrientationsInt imageOrientations) {
  for (eOrientation orientation = imageOrientations.popFirst(); orientation != eOrientation::none; orientation = imageOrientations.pop() )
    if (orientation == eOrientation::landscape) return true;
  return false;
}

cTvMedia cScraperVideoImp::getImage(cImageLevels imageLevels, cOrientations imageOrientations, bool fullPath)
{
  cTvMedia image;
  if (!m_movieOrTv) return orientationsIncludesLandscape(imageOrientations)?getEpgImage(m_event, fullPath):image;

  if (m_collectionId == -2 && m_movieOrTv->getType() == tMovie) {
    m_runtime = 0;
    m_collectionId = 0;
    m_movieOrTv->getOverview(NULL, NULL, NULL, &m_runtime, NULL, &m_collectionId);
  }
  if (fullPath)
    m_movieOrTv->getSingleImageBestLO(imageLevels, imageOrientations, NULL, &image.path, &image.width, &image.height);
  else
    m_movieOrTv->getSingleImageBestLO(imageLevels, imageOrientations, &image.path, NULL, &image.width, &image.height);
  if (image.path.empty() && orientationsIncludesLandscape(imageOrientations)) return getEpgImage(m_event, fullPath);
  return image;
}

std::vector<cTvMedia> cScraperVideoImp::getImages(eOrientation orientation, int maxImages, bool fullPath) {
  std::vector<cTvMedia> result;
  if (!m_movieOrTv) return result;
  return m_movieOrTv->getImages(orientation, maxImages, fullPath);
}

int cScraperVideoImp::getHD() {
  if (!m_sEventOrRecording) return -1;
  return config.ChannelHD(m_sEventOrRecording->ChannelID() );
}
int cScraperVideoImp::getLanguage() {
  if (!m_sEventOrRecording) return -1;
  return config.GetLanguage_n(m_sEventOrRecording->ChannelID() );
}

std::vector<std::unique_ptr<cCharacter>> cScraperVideoImp::getCharacters(bool fullPath) {
  std::vector<std::unique_ptr<cCharacter>> result;
  if (!m_movieOrTv) return result;
  std::vector<cActor> guestActors;
  m_movieOrTv->AddGuestActors(guestActors, fullPath);
  for (const auto &actor: guestActors)
    result.push_back(std::make_unique<cCharacterImp>(eCharacterType::guestStar, actor.name, actor.role, actor.actorThumb));
  for (const auto &actor: m_movieOrTv->GetActors(fullPath))
    result.push_back(std::make_unique<cCharacterImp>(eCharacterType::actor, actor.name, actor.role, actor.actorThumb));
  if (m_movieOrTv->getType() != tMovie && !isEpisodeIdentified() ) return result;
  char *director_ = NULL;
  char *writer_ = NULL;
  sqlite3_stmt *statement;
  if (m_movieOrTv->getType() == tMovie) {
    statement = m_db->QueryPrepare("select movie_director, movie_writer from movie_runtime2 where movie_id = ?", "i", m_movieOrTv->dbID() );
  } else {
    const char *sql_e = "select episode_director, episode_writer from tv_s_e where tv_id = ? and season_number = ? and episode_number = ?";
    statement = m_db->QueryPrepare(sql_e, "iii", m_movieOrTv->dbID(), m_movieOrTv->getSeason(), m_movieOrTv->getEpisode() );
  }
  if (!m_db->QueryStep(statement, "ss", &director_, &writer_) ) return result;
  std::vector<std::string> directors;
  stringToVector(directors, director_);
  for (const auto &director: directors)
    result.push_back(std::make_unique<cCharacterImp>(eCharacterType::director, director));
  std::vector<std::string> writers;
  stringToVector(writers, writer_);
  for (const auto &writer: writers)
    result.push_back(std::make_unique<cCharacterImp>(eCharacterType::writer, writer));
  sqlite3_finalize(statement);
  return result;
}

bool cScraperVideoImp::getMovieOrTv(std::string *title, std::string *originalTitle, std::string *tagline, std::string *overview, std::vector<std::string> *genres, std::string *homepage, std::string *releaseDate, bool *adult, int *runtime, float *popularity, float *voteAverage, int *voteCount, std::vector<std::string> *productionCountries, std::string *imdbId, int *budget, int *revenue, int *collectionId, std::string *collectionName, std::string *status, std::vector<std::string> *networks, int *lastSeason)
{
// only for tMovie: runtime, adult, collection*, tagline, budget, revenue, homepage, productionCountries
// only for tSeries: status, networks, createdBy
  m_runtime = 0;
  m_collectionId = 0;
  if (!m_movieOrTv) return false;
  char *title_ = NULL;
  char *originalTitle_ = NULL;
  char *tagline_ = NULL;
  char *overview_ = NULL;
  char *genres_ = NULL;
  char *homepage_ = NULL;
  char *releaseDate_ = NULL;
  bool adult_ = false;
  float popularity_ = 0;
  float voteAverage_ = 0;
  int voteCount_ = 0;
  char *productionCountries_ = NULL;
  char *imdbId_ = NULL;
  int budget_ = 0;
  int revenue_ = 0;
  char *collectionName_ = NULL;
  char *status_ = NULL;
  char *networks_ = NULL;
  char *createdBy_ = NULL;
  char *posterUrl_ = NULL;
  char *fanartUrl_ = NULL;
  int lastSeason_ = 0;
  sqlite3_stmt *statement;
  if (m_movieOrTv->getType() == tMovie) {
    const char *sql_m = "select movie_title, movie_original_title, movie_tagline, movie_overview, " \
      "movie_adult, movie_collection_id, movie_collection_name, " \
      "movie_budget, movie_revenue, movie_genres, movie_homepage, " \
      "movie_release_date, movie_runtime, movie_popularity, movie_vote_average, movie_vote_count, " \
      "movie_production_countries, movie_posterUrl, movie_fanartUrl, movie_IMDB_ID from movies3 where movie_id = ?";
    statement = m_db->QueryPrepare(sql_m, "i", m_movieOrTv->dbID() );
    if (!m_db->QueryStep(statement, "ssssbisiisssiffissss",
      &title_, &originalTitle_, &tagline_, &overview_, &adult_, &m_collectionId, &collectionName_,
      &budget_, &revenue_, &genres_, &homepage_, &releaseDate_, &m_runtime, &popularity_, &voteAverage_, &voteCount_,
      &productionCountries_, &posterUrl_, &fanartUrl_, &imdbId_) ) return false;
    m_movieOrTv->m_collectionId = m_collectionId;
  } else {
    if (m_movieOrTv->getType() == tSeries) {
      const char *sql_s = "select tv_name, tv_original_name, tv_overview, tv_first_air_date, " \
        "tv_networks, tv_genres, tv_created_by, tv_popularity, tv_vote_average, tv_vote_count, " \
        "tv_posterUrl, tv_fanartUrl, tv_IMDB_ID, tv_status, tv_last_season " \
        "from tv2 where tv_id = ?";
      statement = m_db->QueryPrepare(sql_s, "i", m_movieOrTv->dbID() );
      if (!m_db->QueryStep(statement, "sssssssffissssi",
        &title_, &originalTitle_, &overview_, &releaseDate_, &networks_, &genres_, &createdBy_,
        &popularity_, &voteAverage_, &voteCount_, &posterUrl_, &fanartUrl_, &imdbId_, &status_, &lastSeason_) ) return false;
    } else return false;
  }
  if (title) *title = charPointerToString(title_);
  if (originalTitle) *originalTitle = charPointerToString(originalTitle_);
  if (tagline) *tagline = charPointerToString(tagline_);
  if (overview) *overview = charPointerToString(overview_);
  if (genres) { genres->clear(); stringToVector(*genres, genres_); }
  if (homepage) *homepage = charPointerToString(homepage_);
  if (releaseDate) *releaseDate = charPointerToString(releaseDate_);
  if (adult) *adult = adult_;
  if (runtime) *runtime = m_runtime;
  if (popularity) *popularity = popularity_;
  if (voteAverage) *voteAverage = voteAverage_;
  if (voteCount) *voteCount = voteCount_;
  if (productionCountries) { productionCountries->clear(); stringToVector(*productionCountries, productionCountries_); }
  if (imdbId) *imdbId = charPointerToString(imdbId_);
  if (budget) *budget = budget_;
  if (revenue) *revenue = revenue_;
  if (collectionId) *collectionId = m_collectionId;
  if (collectionName) *collectionName = charPointerToString(collectionName_);
  if (status) *status = charPointerToString(status_);
  if (networks) { networks->clear(); stringToVector(*networks, networks_); }
/*
  if (posterUrl) {
    *posterUrl = ""; // "default", if there is nothing better ...
    if (isEpisodeIdentified() ) {  // we use the season poster of this season, if available
      char *posterUrlSp_;
      const char sql_sp[] = "select media_path from tv_media where tv_id = ? and media_number = ? and media_type = ?";
      sqlite3_stmt *statement_sp = m_db->QueryPrepare(sql_sp, "iii", m_movieOrTv->dbID(), m_movieOrTv->getSeason(), mediaSeason);
      if (m_db->QueryStep(statement_sp, "s", &posterUrlSp_)) {
        if (posterUrlSp_ && *posterUrlSp_) *posterUrl = m_movieOrTv->imageUrl(posterUrlSp_);
        sqlite3_finalize(statement_sp);
      }
    }
    if (posterUrl->empty() && posterUrl_ && *posterUrl_) *posterUrl = m_movieOrTv->imageUrl(posterUrl_);
    if (posterUrl->empty() && m_movieOrTv->getType() == tSeries) {
// no poster was found. Use first season poster
    const char sql_spa[] =
      "select media_path from tv_media where tv_id = ? and media_number >= 0 and media_type = ?";
    for (sqlite3_stmt *statementPoster = m_db->QueryPrepare(sql_spa, "ii", m_movieOrTv->dbID(), mediaSeason);
         m_db->QueryStep(statementPoster, "s", &posterUrl_);)
      if (posterUrl_ && *posterUrl_) {
        *posterUrl = m_movieOrTv->imageUrl(posterUrl_);
        sqlite3_finalize(statementPoster);
        break;
      }
    }
  }
  if (fanartUrl) {
    if (fanartUrl_ && *fanartUrl_) *fanartUrl = m_movieOrTv->imageUrl(fanartUrl_);
    else *fanartUrl = "";
  }
*/

  sqlite3_finalize(statement);
  return true;
}

// episode specific information  ===============================================
// only for tvShows / series!!!

int cScraperVideoImp::getEpisodeNumber() {
  if (!m_movieOrTv || m_movieOrTv->getType() != tSeries) return 0;
  return m_movieOrTv->getEpisode();
}
int cScraperVideoImp::getSeasonNumber() {
  if (!m_movieOrTv || m_movieOrTv->getType() != tSeries) return 0;
  return m_movieOrTv->getSeason();
}

bool cScraperVideoImp::isEpisodeIdentified() {
  return m_movieOrTv &&
         m_movieOrTv->getType() == tSeries &&
        (m_movieOrTv->getEpisode() != 0 || m_movieOrTv->getSeason() != 0);
}

bool cScraperVideoImp::getEpisode(std::string *name, std::string *overview, int *absoluteNumber, std::string *firstAired, int *runtime, float *voteAverage, int *voteCount, std::string *imdbId) {
  if (!isEpisodeIdentified() ) return false;

  char *name_;
  char *overview_;
  int absoluteNumber_;
  char *firstAired_;
  int runtime_;
  float voteAverage_;
  int voteCount_;
  char *imdbId_;
  const char *sql_e = "select episode_absolute_number, episode_name, episode_air_date, " \
    "episode_run_time, episode_vote_average, episode_vote_count, episode_overview, " \
    "episode_IMDB_ID " \
    "from tv_s_e where tv_id = ? and season_number = ? and episode_number = ?";
  sqlite3_stmt *statement = m_db->QueryPrepare(sql_e, "iii", m_movieOrTv->dbID(), m_movieOrTv->getSeason(), m_movieOrTv->getEpisode() );
  if (!m_db->QueryStep(statement, "issifiss", &absoluteNumber_,
     &name_, &firstAired_, &runtime_, &voteAverage_, &voteCount_, &overview_, &imdbId_)) return false;
  if (name) *name = charPointerToString(name_);
  if (overview) *overview = charPointerToString(overview_);
  if (absoluteNumber) *absoluteNumber = absoluteNumber_;
  if (firstAired) *firstAired = charPointerToString(firstAired_);
  if (runtime) {
    if (runtime_ > 0) *runtime = runtime_;
    else *runtime = m_runtime_guess;
  }
  if (voteAverage) *voteAverage = voteAverage_;
  if (voteCount) *voteCount = voteCount_;
  if (imdbId) *imdbId = charPointerToString(imdbId_);
/*
  if (imageUrl) {
    if (imageUrl_ && *imageUrl_) *imageUrl = m_movieOrTv->imageUrl(imageUrl_);
    else *imageUrl = "";
  }
*/
  sqlite3_finalize(statement);
  return true;
}
