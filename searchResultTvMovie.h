#include <string>
#include <vector>

struct sMatchWeight {
  float match;
  float weight;
};
class searchResultTvMovie {

public:
  searchResultTvMovie(int id, bool movie, const std::string &year);
  ~searchResultTvMovie();
  void log(const char *title);
  static float normMatch(float x);
  float getMatch() const;
  bool operator< (const searchResultTvMovie &srm) const;
  void setMatchText(int distance) { m_matches[0].match = (1000 - distance) / 1000.; }
  void updateMatchText(int distance);
  void setPopularity(float popularity, float vote_average, int vote_count);
  void setPopularity(float vote_average, int vote_count);
  void setDuration(int durationDeviation) { m_matches[3].match = 1 - normMatch(durationDeviation / 15.); }
  void setActors(int numMatches)  { m_matches[4].match = normMatch(numMatches/4.); }
  void setMatchYear(const std::vector<int> &years, int durationInSec);
  void setMatchEpisode(std::size_t nmatch ) { m_matches[5].match = normMatch(nmatch / 4.); }
  void setBaseNameEquShortText() { m_matches[6].match = 1.0; }
  void setPositionInExternalResult(int positionInExternalResult) { m_matches[7].match = 1.0 - normMatch(positionInExternalResult); }

  void setDelim(char delim) { m_delim = delim; }
  int delim(void) { return m_delim; }
  int id() const { return m_id; }
  bool movie() const { return m_movie; }
  int year() const { return m_year; }
  int m_yearMatch = 0;
private:
  int m_id;
  bool m_movie;
  int m_year;
  sMatchWeight m_matches[8];
  char m_delim = 0;
};