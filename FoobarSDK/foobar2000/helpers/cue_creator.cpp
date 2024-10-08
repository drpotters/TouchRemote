#include "StdAfx.h"
#include "cue_creator.h"
#include "../SDK/chapterizer.h"

static pfc::string8 format_meta(const file_info& p_source, const char* p_name, bool p_allow_space = true) {
	pfc::string8 temp, ret;
	p_source.meta_format(p_name, temp);
	temp.replace_byte('\"', '\'');
	uReplaceString(ret, temp, pfc_infinite, "\x0d\x0a", 2, "\\", 1, false);
	if (!p_allow_space) ret.replace_byte(' ', '_');
	ret.replace_nontext_chars();
	return ret;
}

static bool is_meta_same_everywhere(const cue_creator::t_entry_list & p_list,const char * p_meta)
{
	pfc::string8_fastalloc reference,temp;

	bool first = true;
	for(auto & iter : p_list) {
		if ( ! iter.isTrackAudio() ) continue;

		if ( first ) {
			first = false;
			if (!iter.m_infos.meta_format(p_meta,reference)) return false;
		} else {
			if (!iter.m_infos.meta_format(p_meta,temp)) return false;
			if (strcmp(temp,reference)!=0) return false;
		}
	}

	return true;
}

#define g_eol "\r\n"

namespace cue_creator
{
	pfc::string_formatter create(const t_entry_list& p_list) {
		pfc::string_formatter ret; create(ret, p_list); return ret;
	}
	void create(pfc::string_formatter & p_out,const t_entry_list & p_data)
	{
		if (p_data.get_count() == 0) return;
		bool album_artist_global =	is_meta_same_everywhere(p_data,"album artist"),
			artist_global =			is_meta_same_everywhere(p_data,"artist"),
			album_global =			is_meta_same_everywhere(p_data,"album"),
			genre_global =			is_meta_same_everywhere(p_data,"genre"),
			date_global =			is_meta_same_everywhere(p_data,"date"),
			discid_global =			is_meta_same_everywhere(p_data,"discid"),
			comment_global =		is_meta_same_everywhere(p_data,"comment"),
			catalog_global =		is_meta_same_everywhere(p_data,"catalog"),
			songwriter_global =		is_meta_same_everywhere(p_data,"songwriter");

		{
			auto firstTrack = p_data.first();
			while( firstTrack.is_valid() && ! firstTrack->isTrackAudio() ) ++ firstTrack;
			if ( firstTrack.is_valid() ) {
				if (genre_global) {
					p_out << "REM GENRE " << format_meta(firstTrack->m_infos,"genre") << g_eol;
				}
				if (date_global) {
					p_out << "REM DATE " << format_meta(firstTrack->m_infos,"date") << g_eol;
				}
				if (discid_global) {
					p_out << "REM DISCID " << format_meta(firstTrack->m_infos,"discid") << g_eol;
				}
				if (comment_global) {
					p_out << "REM COMMENT " << format_meta(firstTrack->m_infos,"comment") << g_eol;
				}
				if (is_meta_same_everywhere(p_data, "discnumber")) {
					p_out << "REM DISCNUMBER " << format_meta(firstTrack->m_infos, "discnumber") << g_eol;
				}
				if (is_meta_same_everywhere(p_data, "totaldiscs")) {
					p_out << "REM TOTALDISCS " << format_meta(firstTrack->m_infos, "totaldiscs") << g_eol;
				}
				if (catalog_global) {
					p_out << "CATALOG " << format_meta(firstTrack->m_infos,"catalog") << g_eol;
				}
				if (songwriter_global) {
					p_out << "SONGWRITER \"" << format_meta(firstTrack->m_infos,"songwriter") << "\"" << g_eol;
				}

				if (album_artist_global)
				{
					p_out << "PERFORMER \"" << format_meta(firstTrack->m_infos,"album artist") << "\"" << g_eol;
					artist_global = false;
				}
				else if (artist_global)
				{
					p_out << "PERFORMER \"" << format_meta(firstTrack->m_infos,"artist") << "\"" << g_eol;
				}
				if (album_global)
				{
					p_out << "TITLE \"" << format_meta(firstTrack->m_infos,"album") << "\"" << g_eol;
				}

				{
					replaygain_info::t_text_buffer rgbuffer;
					replaygain_info rg = firstTrack->m_infos.get_replaygain();
					if (rg.format_album_gain(rgbuffer))
						p_out << "REM REPLAYGAIN_ALBUM_GAIN " << rgbuffer << g_eol;
					if (rg.format_album_peak(rgbuffer))
						p_out << "REM REPLAYGAIN_ALBUM_PEAK " << rgbuffer << g_eol;			
				}

			}
		}

		pfc::string8 last_file;

		for(t_entry_list::const_iterator iter = p_data.first();iter.is_valid();++iter)
		{
			if (strcmp(last_file,iter->m_file) != 0)
			{
				auto fileType = iter->m_fileType;
				if ( fileType.length() == 0 ) fileType = "WAVE";
				p_out << "FILE \"" << iter->m_file << "\" " << fileType << g_eol;
				last_file = iter->m_file;
			}

			{
				auto trackType = iter->m_trackType;
				if (trackType.length() == 0) trackType = "AUDIO";
				p_out << "  TRACK " << pfc::format_int(iter->m_track_number,2) << " " << trackType << g_eol;
			}

			

			if (iter->m_infos.meta_find("title") != pfc_infinite)
				p_out << "    TITLE \"" << format_meta(iter->m_infos,"title") << "\"" << g_eol;
			
			const bool bHaveArtist = iter->m_infos.meta_exists("artist");
			if (!artist_global && bHaveArtist) {
				p_out << "    PERFORMER \"" << format_meta(iter->m_infos,"artist") << "\"" << g_eol;
			} else if (album_artist_global && !bHaveArtist) {
				// special case: album artist set, track artist not set
				p_out << "    PERFORMER \"\"" << g_eol;
			}

			if (!songwriter_global && iter->m_infos.meta_find("songwriter") != pfc_infinite) {
				p_out << "    SONGWRITER \"" << format_meta(iter->m_infos,"songwriter") << "\"" << g_eol;
			}

			if (iter->m_infos.meta_find("isrc") != pfc_infinite) {
				p_out << "    ISRC " << format_meta(iter->m_infos,"isrc") << g_eol;
			}

			if (!date_global && iter->m_infos.meta_find("date") != pfc_infinite) {
				p_out << "    REM DATE " << format_meta(iter->m_infos,"date") << g_eol;
			}
			if (!comment_global && iter->m_infos.meta_exists("comment")) {
				p_out << "    REM COMMENT " << format_meta(iter->m_infos, "comment") << g_eol;
			}


			{
				replaygain_info::t_text_buffer rgbuffer;
				replaygain_info rg = iter->m_infos.get_replaygain();
				if (rg.format_track_gain(rgbuffer))
					p_out << "    REM REPLAYGAIN_TRACK_GAIN " << rgbuffer << g_eol;
				if (rg.format_track_peak(rgbuffer))
					p_out << "    REM REPLAYGAIN_TRACK_PEAK " << rgbuffer << g_eol;			
			}

			if (!iter->m_flags.is_empty()) {
				p_out << "    FLAGS " << iter->m_flags << g_eol;
			}

			if (iter->m_index_list.m_positions[0] < iter->m_index_list.m_positions[1])
			{
				if (iter->m_index_list.m_positions[0] < 0)
					p_out << "    PREGAP " << cuesheet_format_index_time(iter->m_index_list.m_positions[1] - iter->m_index_list.m_positions[0]) << g_eol;
				else
					p_out << "    INDEX 00 " << cuesheet_format_index_time(iter->m_index_list.m_positions[0]) << g_eol;
			}

			p_out << "    INDEX 01 " << cuesheet_format_index_time(iter->m_index_list.m_positions[1]) << g_eol;

			for(unsigned n=2;n<t_cuesheet_index_list::count && iter->m_index_list.m_positions[n] > 0;n++)
			{
				p_out << "    INDEX " << pfc::format_uint(n,2) << " " << cuesheet_format_index_time(iter->m_index_list.m_positions[n]) << g_eol;
			}

			// p_out << "    INDEX 01 " << cuesheet_format_index_time(iter->m_offset) << g_eol;
		}
	}


	void t_entry::set_simple_index(double p_time)
	{
		m_index_list.reset();
		m_index_list.m_positions[0] = m_index_list.m_positions[1] = p_time;
	}
	void t_entry::set_index01(double index0, double index1) {
		PFC_ASSERT( index0 <= index1 );
		m_index_list.reset();
		m_index_list.m_positions[0] = index0;
		m_index_list.m_positions[1] = index1;
	}

	bool t_entry::isTrackAudio() const { 
		PFC_ASSERT( m_trackType.length() > 0 );
		return pfc::stringEqualsI_ascii( m_trackType, "AUDIO" ); 
	}
}



