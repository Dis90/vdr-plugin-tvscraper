#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <jansson.h>
#include "moviedbactors.h"

using namespace std;

cMovieDbActors::cMovieDbActors(const char *json) {
    this->json = json;
}

cMovieDbActors::~cMovieDbActors() {
    actors.clear();
}

void cMovieDbActors::ParseJSON(void) {
    json_t *jActors;
    jActors = json_loads(json.c_str(), 0, NULL);
    if (!jActors) return;
    ParseJSON(jActors);
    json_decref(jActors);
    return;
}
void cMovieDbActors::ParseJSON(json_t *jActors) {
    if(!json_is_object(jActors)) {
        return;
    }
    json_t *jCrew = json_object_get(jActors, "crew");
    director = cMovieDbTv::GetCrewMember(jCrew, "job", "Director");
    writer   = cMovieDbTv::GetCrewMember(jCrew, "department", "Writing");
    json_t *cast = json_object_get(jActors, "cast");
    if(!json_is_array(cast)) {
        return;
    }
    size_t numActors = json_array_size(cast);
    for (size_t i = 0; i < numActors; i++) {
        json_t *jActor = json_array_get(cast, i);
        if (!json_is_object(jActor)) {
            return;
        }
        json_t *jId = json_object_get(jActor, "id");
        json_t *jName = json_object_get(jActor, "name");
        json_t *jRole = json_object_get(jActor, "character");
        if (!json_is_integer(jId) || !json_is_string(jName) || !json_is_string(jRole) )
            continue;
        cMovieDBActor *actor = new cMovieDBActor();
        actor->id = json_integer_value(jId);
        actor->name = json_string_value(jName);
        actor->role = json_string_value(jRole);
        actor->path = json_string_value_validated(jActor, "profile_path");
        actors.push_back(actor);
    }
}

void cMovieDbActors::StoreDB(cTVScraperDB *db, int movieID) {
    int numActors = actors.size();
    for (int i=0; i<numActors; i++) {
      db->InsertMovieActor(movieID, actors[i]->id, actors[i]->name, actors[i]->role, !actors[i]->path.empty() );
    }
    db->InsertMovieDirectorWriter(movieID, director, writer);
}

void cMovieDbActors::Store(cTVScraperDB *db, int movieID) {
  for (auto &actor: actors) {
    if (actor->path.empty() ) continue;
    db->AddActorDownload (movieID, true, actor->id, actor->path);
  }
}

void cMovieDbActors::Dump(void) {
    int numActors = actors.size();
    esyslog("tvscraper: %d Actors:", numActors);
    for (int i=0; i<numActors; i++) {
        esyslog("tvscraper: id %d, name %s, role %s, path %s", actors[i]->id, actors[i]->name.c_str(), actors[i]->role.c_str(), actors[i]->path.c_str());
    }
}
