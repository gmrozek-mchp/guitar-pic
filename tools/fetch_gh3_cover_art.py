#!/usr/bin/env python3
"""Fetch GH3 (Wii) cover art via MusicBrainz + the Cover Art Archive.

Stdlib only -- runs anywhere with Python 3.7+, no pip install. Resolves each
song's (artist, album) to a MusicBrainz release-group, then downloads the front
cover from coverartarchive.org. Files are named <setlist>_<idx>_<slug>.<ext>,
matching the recognizer's stable (setlist, index) key.

With --catalog it also (re)generates the marvin song catalog CSV (spec §4.8.3),
sourcing the original-release year and a genre tag from MusicBrainz in the same
pass; title/artist/album come from the SONGS table below and `difficulty` is
left blank (GH3 exposes no per-song rating). Without network access the catalog
can still be written (year=0, genre/difficulty blank) by passing --catalog with
--no-enrich.

Usage:
    # IMPORTANT: set a real contact string -- MusicBrainz blocks generic agents.
    python3 fetch_gh3_cover_art.py --contact you@example.com
    python3 fetch_gh3_cover_art.py --contact you@example.com --out ./art --size 1200
    # also fill the catalog (year + genre) from MusicBrainz:
    python3 fetch_gh3_cover_art.py --contact you@example.com \\
        --catalog ../firmware/marvin/data/games/gh3-wii/songs.csv

MusicBrainz asks for <=1 request/sec; this script self-throttles. Misses are
logged to <out>/_misses.txt for manual handling (esp. the obscure bonus tracks).
"""

import argparse
import json
import os
import sys
import time
import urllib.parse
import urllib.request

# (setlist, index, slug, title, artist, album). Empty album => recording-only
# fallback (promo/contest tracks with no clean commercial cover).
SONGS = [
    ("main", 0,  "slow_ride",                 "Slow Ride",                 "Foghat",                  "Fool for the City"),
    ("main", 1,  "talk_dirty_to_me",          "Talk Dirty to Me",          "Poison",                  "Look What the Cat Dragged In"),
    ("main", 2,  "hit_me_with_your_best_shot","Hit Me with Your Best Shot","Pat Benatar",             "Crimes of Passion"),
    ("main", 3,  "story_of_my_life",          "Story of My Life",          "Social Distortion",       "Social Distortion"),
    ("main", 4,  "rock_and_roll_all_nite",    "Rock and Roll All Nite",    "Kiss",                    "Alive!"),
    ("main", 5,  "mississippi_queen",         "Mississippi Queen",         "Mountain",                "Climbing!"),
    ("main", 6,  "school_s_out",              "School's Out",              "Alice Cooper",            "School's Out"),
    ("main", 7,  "sunshine_of_your_love",     "Sunshine of Your Love",     "Cream",                   "Disraeli Gears"),
    ("main", 8,  "barracuda",                 "Barracuda",                 "Heart",                   "Little Queen"),
    ("main", 9,  "bulls_on_parade",           "Bulls on Parade",           "Rage Against the Machine","Evil Empire"),
    ("main", 10, "when_you_were_young",       "When You Were Young",       "The Killers",             "Sam's Town"),
    ("main", 11, "miss_murder",               "Miss Murder",               "AFI",                     "Decemberunderground"),
    ("main", 12, "the_seeker",                "The Seeker",                "The Who",                 "Meaty Beaty Big and Bouncy"),
    ("main", 13, "lay_down",                  "Lay Down",                  "Priestess",               "Hello Master"),
    ("main", 14, "paint_it_black",            "Paint It Black",            "The Rolling Stones",      "Aftermath"),
    ("main", 15, "paranoid",                  "Paranoid",                  "Black Sabbath",           "Paranoid"),
    ("main", 16, "anarchy_in_the_u_k",        "Anarchy in the U.K.",       "Sex Pistols",             "Never Mind the Bollocks, Here's the Sex Pistols"),
    ("main", 17, "kool_thing",                "Kool Thing",                "Sonic Youth",             "Goo"),
    ("main", 18, "my_name_is_jonas",          "My Name Is Jonas",          "Weezer",                  "Weezer"),
    ("main", 19, "even_flow",                 "Even Flow",                 "Pearl Jam",               "Ten"),
    ("main", 20, "holiday_in_cambodia",       "Holiday in Cambodia",       "Dead Kennedys",           "Fresh Fruit for Rotting Vegetables"),
    ("main", 21, "rock_you_like_a_hurricane", "Rock You Like a Hurricane", "Scorpions",               "Love at First Sting"),
    ("main", 22, "same_old_song_and_dance",   "Same Old Song and Dance",   "Aerosmith",               "Get Your Wings"),
    ("main", 23, "la_grange",                 "La Grange",                 "ZZ Top",                  "Tres Hombres"),
    ("main", 24, "welcome_to_the_jungle",     "Welcome to the Jungle",     "Guns N' Roses",           "Appetite for Destruction"),
    ("main", 25, "black_magic_woman",         "Black Magic Woman",         "Santana",                 "Abraxas"),
    ("main", 26, "cherub_rock",               "Cherub Rock",               "The Smashing Pumpkins",   "Siamese Dream"),
    ("main", 27, "black_sunshine",            "Black Sunshine",            "White Zombie",            "La Sexorcisto: Devil Music Volume One"),
    ("main", 28, "the_metal",                 "The Metal",                 "Tenacious D",             "The Pick of Destiny"),
    ("main", 29, "pride_and_joy",             "Pride and Joy",             "Stevie Ray Vaughan",      "Texas Flood"),
    ("main", 30, "before_i_forget",           "Before I Forget",           "Slipknot",                "Vol. 3: (The Subliminal Verses)"),
    ("main", 31, "stricken",                  "Stricken",                  "Disturbed",               "Ten Thousand Fists"),
    ("main", 32, "3_s_7_s",                   "3's & 7's",                 "Queens of the Stone Age", "Era Vulgaris"),
    ("main", 33, "knights_of_cydonia",        "Knights of Cydonia",        "Muse",                    "Black Holes and Revelations"),
    ("main", 34, "cult_of_personality",       "Cult of Personality",       "Living Colour",           "Vivid"),
    ("main", 35, "raining_blood",             "Raining Blood",             "Slayer",                  "Reign in Blood"),
    ("main", 36, "cliffs_of_dover",           "Cliffs of Dover",           "Eric Johnson",            "Ah Via Musicom"),
    ("main", 37, "the_number_of_the_beast",   "The Number of the Beast",   "Iron Maiden",             "The Number of the Beast"),
    ("main", 38, "one",                       "One",                       "Metallica",               "...And Justice for All"),

    ("bonus", 0,  "avalancha",                "Avalancha",                 "Héroes del Silencio",     "Avalancha"),
    ("bonus", 1,  "in_the_belly_of_a_shark",  "In the Belly of a Shark",   "Gallows",                 "Orchestra of Wolves"),
    ("bonus", 2,  "can_t_be_saved",           "Can't Be Saved",            "Senses Fail",             "Still Searching"),
    ("bonus", 3,  "closer",                   "Closer",                    "Lacuna Coil",             "Karmacode"),
    ("bonus", 4,  "don_t_hold_back",          "Don't Hold Back",           "The Sleeping",            "Questions and Answers"),
    ("bonus", 5,  "down_n_dirty",             "Down 'n Dirty",             "L.A. Slum Lords",         ""),
    ("bonus", 6,  "f_c_p_r_e_m_i_x",          "F.C.P.R.E.M.I.X.",          "The Fall of Troy",        "Doppelgänger"),
    ("bonus", 7,  "generation_rock",          "Generation Rock",           "Revolverheld",            "Revolverheld"),
    ("bonus", 8,  "go_that_far",              "Go That Far",               "Bret Michaels",           "Rock My World"),
    ("bonus", 9,  "hier_kommt_alex",          "Hier kommt Alex",           "Die Toten Hosen",         "Ein kleines bisschen Horrorschau"),
    ("bonus", 10, "i_m_in_the_band",          "I'm in the Band",           "The Hellacopters",        "Rock & Roll Is Dead"),
    ("bonus", 11, "impulse",                  "Impulse",                   "An Endless Sporadic",     "An Endless Sporadic"),
    ("bonus", 12, "in_love",                  "In Love",                   "Scouts of St. Sebastian", ""),
    ("bonus", 13, "mauvais_gar_on",           "Mauvais Garçon",            "Naast",                   "Antichambre"),
    ("bonus", 14, "metal_heavy_lady",         "Metal Heavy Lady",          "Lions",                   "Lions"),
    ("bonus", 15, "minus_celsius",            "Minus Celsius",             "Backyard Babies",         "Stockholm Syndrome"),
    ("bonus", 16, "my_curse",                 "My Curse",                  "Killswitch Engage",       "As Daylight Dies"),
    ("bonus", 17, "nothing_for_me_here",      "Nothing for Me Here",       "Dope",                    "No Regrets"),
    ("bonus", 18, "prayer_of_the_refugee",    "Prayer of the Refugee",     "Rise Against",            "The Sufferer & the Witness"),
    ("bonus", 19, "radio_song",               "Radio Song",                "Superbus",                "Pop'n'gum"),
    ("bonus", 20, "ruby",                     "Ruby",                      "Kaiser Chiefs",           "Yours Truly, Angry Mob"),
    ("bonus", 21, "she_bangs_the_drums",      "She Bangs the Drums",       "The Stone Roses",         "The Stone Roses"),
    ("bonus", 22, "take_this_life",           "Take This Life",            "In Flames",               "Come Clarity"),
    ("bonus", 23, "the_way_it_ends",          "The Way It Ends",           "Prototype",               "Continuum"),
    ("bonus", 24, "through_the_fire_and_flames","Through the Fire and Flames","DragonForce",          "Inhuman Rampage"),
]

MB_ROOT = "https://musicbrainz.org/ws/2"
CAA_ROOT = "https://coverartarchive.org"

_last_mb_call = [0.0]


def mb_get(path, params, user_agent, min_interval=1.1):
    """GET a MusicBrainz endpoint as JSON, throttled to <=1 req/sec."""
    wait = min_interval - (time.time() - _last_mb_call[0])
    if wait > 0:
        time.sleep(wait)
    params = dict(params, fmt="json")
    url = "%s/%s?%s" % (MB_ROOT, path, urllib.parse.urlencode(params))
    req = urllib.request.Request(url, headers={"User-Agent": user_agent})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = json.load(resp)
    finally:
        _last_mb_call[0] = time.time()
    return data


def lucene_escape(s):
    for ch in r'+-&|!(){}[]^"~*?:\/':
        s = s.replace(ch, "\\" + ch)
    return s


def find_release_groups(artist, album, user_agent):
    """Return candidate release-group MBIDs, best score first."""
    q = 'releasegroup:"%s" AND artist:"%s"' % (lucene_escape(album), lucene_escape(artist))
    data = mb_get("release-group", {"query": q, "limit": 5}, user_agent)
    return [rg["id"] for rg in data.get("release-groups", [])]


def find_releases_via_recording(artist, title, user_agent):
    """Fallback: find releases that carry a recording of <title> by <artist>."""
    q = 'recording:"%s" AND artist:"%s"' % (lucene_escape(title), lucene_escape(artist))
    data = mb_get("recording", {"query": q, "limit": 10}, user_agent)
    ids = []
    for rec in data.get("recordings", []):
        for rel in rec.get("releases", []):
            if rel["id"] not in ids:
                ids.append(rel["id"])
    return ids[:5]


def caa_fetch(kind, mbid, size, user_agent):
    """Download the front cover for a release-group/release. Returns (bytes, ext) or None."""
    suffix = "front-%d" % size if size else "front"
    url = "%s/%s/%s/%s" % (CAA_ROOT, kind, mbid, suffix)
    req = urllib.request.Request(url, headers={"User-Agent": user_agent})
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            blob = resp.read()
            ctype = resp.headers.get("Content-Type", "")
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise
    ext = "png" if "png" in ctype else "jpg"
    return blob, ext


def mb_first_year(artist, album, title, user_agent):
    """Best-effort original-release year (4-digit int), or 0 if unknown.

    Prefers the album's release-group first-release-date; falls back to the
    earliest release date of any recording of the song. Takes the minimum so a
    later compilation/reissue doesn't win over the original."""
    years = []
    if album:
        q = 'releasegroup:"%s" AND artist:"%s"' % (lucene_escape(album), lucene_escape(artist))
        data = mb_get("release-group", {"query": q, "limit": 5}, user_agent)
        for rg in data.get("release-groups", []):
            d = (rg.get("first-release-date") or "")[:4]
            if d.isdigit():
                years.append(int(d))
    if not years:
        q = 'recording:"%s" AND artist:"%s"' % (lucene_escape(title), lucene_escape(artist))
        data = mb_get("recording", {"query": q, "limit": 10}, user_agent)
        for rec in data.get("recordings", []):
            d = (rec.get("first-release-date") or "")[:4]
            if d.isdigit():
                years.append(int(d))
    return min(years) if years else 0


def mb_genre(artist, user_agent):
    """Highest-count MusicBrainz genre for the artist (title-cased), or ''.

    Genre lives most reliably at the artist level; falls back to free-text tags."""
    data = mb_get("artist", {"query": 'artist:"%s"' % lucene_escape(artist), "limit": 1}, user_agent)
    arts = data.get("artists", [])
    if not arts:
        return ""
    info = mb_get("artist/%s" % arts[0]["id"], {"inc": "genres tags"}, user_agent)
    for key in ("genres", "tags"):
        items = info.get(key) or []
        if items:
            best = max(items, key=lambda g: g.get("count", 0))
            name = best.get("name", "")
            if name:
                return name.title()
    return ""


def write_catalog(path, rows):
    """Write the song catalog CSV. rows: (setlist, index, title, artist, album,
    bpm, length_s, year, genre, difficulty). Quotes the free-text columns and
    doubles embedded quotes; numerics stay bare. Matches the on-device parser
    (firmware/marvin/default/src/util/csv.c)."""
    def q(s):
        return '"' + (s or "").replace('"', '""') + '"'
    lines = ["setlist,index,title,artist,album,bpm,length_s,year,genre,difficulty"]
    for setlist, index, title, artist, album, bpm, length_s, year, genre, diff in rows:
        lines.append("%s,%d,%s,%s,%s,%d,%d,%d,%s,%s" % (
            setlist, index, q(title), q(artist), q(album), bpm, length_s, year, q(genre), q(diff)))
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--contact", required=True,
                    help="Your email or project URL (required by the MusicBrainz API)")
    ap.add_argument("--out", default="gh3_cover_art", help="Output directory")
    ap.add_argument("--size", type=int, default=500, choices=[250, 500, 1200, 0],
                    help="Cover size in px; 0 = full resolution (default 500)")
    ap.add_argument("--force", action="store_true", help="Re-download even if file exists")
    ap.add_argument("--catalog", metavar="PATH",
                    help="Also (re)generate the song catalog CSV at PATH (spec §4.8.3)")
    ap.add_argument("--no-enrich", action="store_true",
                    help="With --catalog, skip MusicBrainz year/genre lookups (write blanks)")
    args = ap.parse_args()

    user_agent = "gh3-coverart/1.0 ( %s )" % args.contact
    os.makedirs(args.out, exist_ok=True)

    catalog_rows = [] if args.catalog else None

    hits, misses = 0, []
    for setlist, idx, slug, title, artist, album in SONGS:
        base = "%s_%02d_%s" % (setlist, idx, slug)

        # Catalog metadata is sourced for every song regardless of art outcome.
        if catalog_rows is not None:
            year, genre = 0, ""
            if not args.no_enrich:
                try:
                    year = mb_first_year(artist, album, title, user_agent)
                    genre = mb_genre(artist, user_agent)
                except Exception as e:  # network hiccup -- leave blank, keep going
                    print("   ! meta error: %s" % e, file=sys.stderr)
            catalog_rows.append((setlist, idx, title, artist, album, 0, 0, year, genre, ""))
        existing = [f for f in os.listdir(args.out) if f.startswith(base + ".")]
        if existing and not args.force:
            print("  skip  %-30s (have %s)" % (base, existing[0]))
            hits += 1
            continue

        print("fetch  %-30s  %s - %s" % (base, artist, album or "(no album)"))
        blob = None
        try:
            # Primary: release-group front cover.
            if album:
                for mbid in find_release_groups(artist, album, user_agent):
                    got = caa_fetch("release-group", mbid, args.size, user_agent)
                    if got:
                        blob = got
                        break
            # Fallback: any release carrying a recording of this song.
            if blob is None:
                for mbid in find_releases_via_recording(artist, title, user_agent):
                    got = caa_fetch("release", mbid, args.size, user_agent)
                    if got:
                        blob = got
                        break
        except Exception as e:  # network hiccup, etc. -- log and continue
            print("   ! error: %s" % e, file=sys.stderr)

        if blob is None:
            print("   MISS  no cover found")
            misses.append("%s\t%s\t%s\t%s" % (base, title, artist, album))
            continue

        data, ext = blob
        path = os.path.join(args.out, base + "." + ext)
        with open(path, "wb") as f:
            f.write(data)
        print("   OK    -> %s (%d KB)" % (os.path.basename(path), len(data) // 1024))
        hits += 1

    if catalog_rows is not None:
        write_catalog(args.catalog, catalog_rows)
        filled = sum(1 for r in catalog_rows if r[7] != 0)
        print("\nCatalog: wrote %d songs to %s (year filled for %d)" %
              (len(catalog_rows), args.catalog, filled))

    print("\nDone: %d/%d covered, %d misses." % (hits, len(SONGS), len(misses)))
    if misses:
        miss_path = os.path.join(args.out, "_misses.txt")
        with open(miss_path, "w") as f:
            f.write("base\ttitle\tartist\talbum\n")
            f.write("\n".join(misses) + "\n")
        print("Misses logged to %s -- source these by hand." % miss_path)


if __name__ == "__main__":
    main()
