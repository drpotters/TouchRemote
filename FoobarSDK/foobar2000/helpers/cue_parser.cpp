#include "StdAfx.h"
#include "cue_parser.h"


#define maximumCueTrackNumber 999

namespace {
	PFC_DECLARE_EXCEPTION(exception_cue,pfc::exception,"Invalid cuesheet");
	PFC_DECLARE_EXCEPTION(exception_cue_tracktype, exception_cue, "Not an audio track")
}

[[noreturn]] static void cue_fail(const char* msg) {
	pfc::throw_exception_with_message< exception_cue >(msg);
}

static bool is_numeric(char c) {return c>='0' && c<='9';}


static bool is_spacing(char c)
{
	return c == ' ' || c == '\t';
}

static bool is_linebreak(char c)
{
	return c == '\n' || c == '\r';
}

static void validate_file_type(const char * p_type,t_size p_type_length) {
	const char* const allowedTypes[] = {
		"WAVE", "MP3", "AIFF", // standard typers
		"APE", "FLAC", "WV", "WAVPACK", "MP4", // common user-entered types
		"BINARY" // BINARY
	};
	for (auto walk : allowedTypes) {
		if (pfc::stringEqualsI_ascii_ex(p_type, p_type_length, walk, SIZE_MAX)) return;
	}
	pfc::throw_exception_with_message< exception_cue >(PFC_string_formatter() << "expected WAVE, MP3 or AIFF, got : \"" << pfc::string_part(p_type,p_type_length) << "\"");
}

namespace {
	
	class NOVTABLE cue_parser_callback
	{
	public:
		virtual void on_file(const char * p_file,t_size p_file_length,const char * p_type,t_size p_type_length) = 0;
		virtual void on_track(unsigned p_index,const char * p_type,t_size p_type_length) = 0;
		virtual void on_pregap(unsigned p_value) = 0;
		virtual void on_index(unsigned p_index,unsigned p_value) = 0;
		virtual void on_title(const char * p_title,t_size p_title_length) = 0;
		virtual void on_performer(const char * p_performer,t_size p_performer_length) = 0;
		virtual void on_songwriter(const char * p_songwriter,t_size p_songwriter_length) = 0;
		virtual void on_isrc(const char * p_isrc,t_size p_isrc_length) = 0;
		virtual void on_catalog(const char * p_catalog,t_size p_catalog_length) = 0;
		virtual void on_comment(const char * p_comment,t_size p_comment_length) = 0;
		virtual void on_flags(const char * p_flags,t_size p_flags_length) = 0;
	};

	class NOVTABLE cue_parser_callback_meta : public cue_parser_callback
	{
	public:
		virtual void on_file(const char * p_file,t_size p_file_length,const char * p_type,t_size p_type_length) = 0;
		virtual void on_track(unsigned p_index,const char * p_type,t_size p_type_length) = 0;
		virtual void on_pregap(unsigned p_value) = 0;
		virtual void on_index(unsigned p_index,unsigned p_value) = 0;
		virtual void on_meta(const char * p_name,t_size p_name_length,const char * p_value,t_size p_value_length) = 0;
	protected:
		static bool is_known_meta(const char * p_name,t_size p_length)
		{
			static const char * metas[] = {"genre","date","discid","comment","replaygain_track_gain","replaygain_track_peak","replaygain_album_gain","replaygain_album_peak", "discnumber", "totaldiscs"};
			for (const char* m : metas) {
				if (!stricmp_utf8_ex(p_name, p_length, m, SIZE_MAX)) return true;
			}
			return false;
		}

		void on_comment(const char * p_comment,t_size p_comment_length)
		{
			unsigned ptr = 0;
			while(ptr < p_comment_length && !is_spacing(p_comment[ptr])) ptr++;
			if (is_known_meta(p_comment, ptr))
			{
				unsigned name_length = ptr;
				while(ptr < p_comment_length && is_spacing(p_comment[ptr])) ptr++;
				if (ptr < p_comment_length)
				{
					if (p_comment[ptr] == '\"')
					{
						ptr++;
						unsigned value_base = ptr;
						while(ptr < p_comment_length && p_comment[ptr] != '\"') ptr++;
						if (ptr == p_comment_length) pfc::throw_exception_with_message<exception_cue>("invalid REM syntax");
						if (ptr > value_base) on_meta(p_comment,name_length,p_comment + value_base,ptr - value_base);
					}
					else
					{
						unsigned value_base = ptr;
						while(ptr < p_comment_length /*&& !is_spacing(p_comment[ptr])*/) ptr++;
						if (ptr > value_base) on_meta(p_comment,name_length,p_comment + value_base,ptr - value_base);
					}
				}
			}
		}
		void on_title(const char * p_title,t_size p_title_length)
		{
			on_meta("title",pfc_infinite,p_title,p_title_length);
		}
		void on_songwriter(const char * p_songwriter,t_size p_songwriter_length) {
			on_meta("songwriter",pfc_infinite,p_songwriter,p_songwriter_length);
		}
		void on_performer(const char * p_performer,t_size p_performer_length)
		{
			on_meta("artist",pfc_infinite,p_performer,p_performer_length);
		}

		void on_isrc(const char * p_isrc,t_size p_isrc_length)
		{
			on_meta("isrc",pfc_infinite,p_isrc,p_isrc_length);
		}
		void on_catalog(const char * p_catalog,t_size p_catalog_length)
		{
			on_meta("catalog",pfc_infinite,p_catalog,p_catalog_length);
		}
		void on_flags(const char * p_flags,t_size p_flags_length) {}
	};


	class cue_parser_callback_retrievelist : public cue_parser_callback
	{
	public:
		cue_parser_callback_retrievelist(cue_parser::t_cue_entry_list& p_out) : m_out(p_out) {}
		
		void on_file(const char * p_file,t_size p_file_length,const char * p_type,t_size p_type_length)
		{
			validate_file_type(p_type,p_type_length);
			m_file.set_string(p_file,p_file_length);
			m_fileType.set_string(p_type, p_type_length);
		}
		
		void on_track(unsigned p_index,const char * p_type,t_size p_type_length)
		{
			finalize_track(); // finalize previous track

			m_trackIsAudio = stricmp_utf8_ex(p_type,p_type_length,"audio",pfc_infinite) == 0;
			if (m_file.is_empty()) pfc::throw_exception_with_message<exception_cue>("declaring a track with no file set");
			m_trackfile = m_file;
			m_trackFileType = m_fileType;
			m_track = p_index;
		}

		void on_pregap(unsigned p_value) {m_pregap = (double) p_value / 75.0;}

		void on_index(unsigned p_index,unsigned p_value)
		{
			if (p_index < t_cuesheet_index_list::count)
			{
				switch(p_index)
				{
				case 0: m_index0_set = true; break;
				case 1: m_index1_set = true; break;
				}
				m_index_list.m_positions[p_index] = (double) p_value / 75.0;
			}
		}

		void on_title(const char * p_title,t_size p_title_length) {}
		void on_performer(const char * p_performer,t_size p_performer_length) {}
		void on_songwriter(const char * p_songwriter,t_size p_songwriter_length) {}
		void on_isrc(const char * p_isrc,t_size p_isrc_length) {}
		void on_catalog(const char * p_catalog,t_size p_catalog_length) {}
		void on_comment(const char * p_comment,t_size p_comment_length) {}
		void on_flags(const char * p_flags,t_size p_flags_length) {}

		void finalize()
		{
			finalize_track(); // finalize last track
			sanity();
		}

	private:
		void sanity() {
			int trk = 0;
			for (auto& iter : m_out) {
				int i = iter.m_track_number;
				if (i <= trk) cue_fail("incorrect track numbering");
				trk = i;
			}
		}
		void finalize_track()
		{
			if ( m_track != 0 && m_trackIsAudio ) {
				if (!m_index1_set) cue_fail("INDEX 01 not set");
				if (!m_index0_set) m_index_list.m_positions[0] = m_index_list.m_positions[1] - m_pregap;
				if (!m_index_list.is_valid()) cue_fail("invalid index list");

				cue_parser::t_cue_entry_list::iterator iter;
				iter = m_out.insert_last();
				if (m_trackfile.is_empty()) cue_fail("track has no file assigned");
				iter->m_file = m_trackfile;
				iter->m_fileType = m_trackFileType;
				iter->m_track_number = m_track;
				iter->m_indexes = m_index_list;
			}

			m_index_list.reset();
			m_index0_set = false;
			m_index1_set = false;
			m_pregap = 0;

			m_track = 0; m_trackIsAudio = false;
		}

		bool m_index0_set = false,m_index1_set = false;
		t_cuesheet_index_list m_index_list;
		double m_pregap = 0;
		unsigned m_track = 0;
		bool m_trackIsAudio = false;
		pfc::string8 m_file,m_fileType,m_trackfile,m_trackFileType;
		cue_parser::t_cue_entry_list & m_out;
	};

	class cue_parser_callback_retrieveinfo : public cue_parser_callback_meta
	{
	public:
		cue_parser_callback_retrieveinfo(file_info & p_out,unsigned p_wanted_track) : m_out(p_out), m_wanted_track(p_wanted_track), m_track(0), m_is_va(false), m_index0_set(false), m_index1_set(false), m_pregap(0), m_totaltracks(0) {}

		void on_file(const char * p_file,t_size p_file_length,const char * p_type,t_size p_type_length) {}

		void on_track(unsigned p_index,const char * p_type,t_size p_type_length)
		{
			if (p_index == 0) cue_fail("invalid TRACK index");
			if (p_index == m_wanted_track)
			{
				if (stricmp_utf8_ex(p_type,p_type_length,"audio",pfc_infinite)) throw exception_cue_tracktype();
			}
			m_track = p_index;
			m_totaltracks++;
		}

		void on_pregap(unsigned p_value) {if (m_track == m_wanted_track) m_pregap = (double) p_value / 75.0;}

		void on_index(unsigned p_index,unsigned p_value)
		{
			if (m_track == m_wanted_track && p_index < t_cuesheet_index_list::count)
			{
				switch(p_index)
				{
				case 0:	m_index0_set = true; break;
				case 1: m_index1_set = true; break;
				}
				m_indexes.m_positions[p_index] = (double) p_value / 75.0;
			}
		}

		
		void on_meta(const char * p_name,t_size p_name_length,const char * p_value,t_size p_value_length)
		{
			t_meta_list::iterator iter;
			if (m_track == 0) //globals
			{
				//convert global title to album
				if (!stricmp_utf8_ex(p_name,p_name_length,"title",pfc_infinite))
				{
					p_name = "album";
					p_name_length = 5;
				}
				else if (!stricmp_utf8_ex(p_name,p_name_length,"artist",pfc_infinite))
				{
					m_album_artist.set_string(p_value,p_value_length);
				}

				iter = m_globals.insert_last();
			}
			else
			{
				if (!m_is_va)
				{
					if (!stricmp_utf8_ex(p_name,p_name_length,"artist",pfc_infinite))
					{
						if (!m_album_artist.is_empty())
						{
							if (stricmp_utf8_ex(p_value,p_value_length,m_album_artist,m_album_artist.length())) m_is_va = true;
						}
					}
				}

				if (m_track == m_wanted_track) //locals
				{
					iter = m_locals.insert_last();
				}
			}
			if (iter.is_valid())
			{
				iter->m_name.set_string(p_name,p_name_length);
				iter->m_value.set_string(p_value,p_value_length);
			}
		}

		void _meta_set(const char* key, const pfc::string8 & value) {
			if ( value.length() > 0 ) m_out.meta_set( key, value );
		}

		void finalize()
		{
			if (!m_index1_set) pfc::throw_exception_with_message< exception_cue > ("INDEX 01 not set");
			if (!m_index0_set) m_indexes.m_positions[0] = m_indexes.m_positions[1] - m_pregap;
			m_indexes.to_infos(m_out);

			replaygain_info rg;
			rg.reset();
			t_meta_list::const_iterator iter;

			if (m_is_va)
			{
				//clean up VA mess

				t_meta_list::const_iterator iter_global,iter_local;

				iter_global = find_first_field(m_globals,"artist");
				iter_local = find_first_field(m_locals,"artist");
				if (iter_global.is_valid())
				{
					_meta_set("album artist",iter_global->m_value);
					if (iter_local.is_valid()) {
						_meta_set("artist",iter_local->m_value);
					} else {
						_meta_set("artist",iter_global->m_value);
					}
				}
				else
				{
					if (iter_local.is_valid()) _meta_set("artist",iter_local->m_value);
				}
				

				wipe_field(m_globals,"artist");
				wipe_field(m_locals,"artist");
				
			}

			for(iter=m_globals.first();iter.is_valid();iter++)
			{
				if (!rg.set_from_meta(iter->m_name,iter->m_value))
					_meta_set(iter->m_name,iter->m_value);
			}
			for(iter=m_locals.first();iter.is_valid();iter++)
			{
				if (!rg.set_from_meta(iter->m_name,iter->m_value))
					_meta_set(iter->m_name,iter->m_value);
			}
			m_out.meta_set("tracknumber",PFC_string_formatter() << m_wanted_track);
			m_out.meta_set("totaltracks", PFC_string_formatter() << m_totaltracks);
			m_out.set_replaygain(rg);

		}
	private:
		struct t_meta_entry {
			pfc::string8 m_name,m_value;
		};
		typedef pfc::chain_list_v2_t<t_meta_entry> t_meta_list;

		static t_meta_list::const_iterator find_first_field(t_meta_list const & p_list,const char * p_field)
		{
			t_meta_list::const_iterator iter;
			for(iter=p_list.first();iter.is_valid();++iter)
			{
				if (!stricmp_utf8(p_field,iter->m_name)) return iter;
			}
			return t_meta_list::const_iterator();//null iterator
		}

		static void wipe_field(t_meta_list & p_list,const char * p_field)
		{
			t_meta_list::iterator iter;
			for(iter=p_list.first();iter.is_valid();)
			{
				if (!stricmp_utf8(p_field,iter->m_name))
				{
					t_meta_list::iterator temp = iter;
					++temp;
					p_list.remove_single(iter);
					iter = temp;
				}
				else
				{
					++iter;
				}
			}
		}
		
		t_meta_list m_globals,m_locals;
		file_info & m_out;
		unsigned m_wanted_track, m_track,m_totaltracks;
		pfc::string8 m_album_artist;
		bool m_is_va;
		t_cuesheet_index_list m_indexes;
		bool m_index0_set,m_index1_set;
		double m_pregap;
	};

};

static pfc::string_part_ref cue_line_argument( const char * base, size_t length ) {
	const char * end = base + length;
	while(base < end && is_spacing(base[0]) ) ++base;
	while(base < end && is_spacing(end[-1]) ) --end;
	if ( base + 1 < end ) {
		if ( base[0] == '\"' && end[-1] == '\"' ) {
			++base; --end;
		}
	}
	return pfc::string_part(base, end-base);
}

static void g_parse_cue_line(const char * p_line,t_size p_line_length,cue_parser_callback & p_callback)
{
	t_size ptr = 0;
	while(ptr < p_line_length && !is_spacing(p_line[ptr])) ptr++;
	if (!stricmp_utf8_ex(p_line,ptr,"file",pfc_infinite))
	{
		while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;
		t_size file_base,file_length, type_base,type_length;
		
		if (p_line[ptr] == '\"')
		{
			ptr++;
			file_base = ptr;
			while(ptr < p_line_length && p_line[ptr] != '\"') ptr++;
			if (ptr == p_line_length) pfc::throw_exception_with_message< exception_cue > ("invalid FILE syntax");
			file_length = ptr - file_base;
			ptr++;
			while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;
		}
		else
		{
			file_base = ptr;
			while(ptr < p_line_length && !is_spacing(p_line[ptr])) ptr++;
			file_length = ptr - file_base;
			while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;
		}

		type_base = ptr;
		while(ptr < p_line_length && !is_spacing(p_line[ptr])) ptr++;
		type_length = ptr - type_base;
		while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;

		if (ptr != p_line_length || file_length == 0 || type_length == 0) pfc::throw_exception_with_message< exception_cue > ("invalid FILE syntax");

		p_callback.on_file(p_line + file_base, file_length, p_line + type_base, type_length);
	}
	else if (!stricmp_utf8_ex(p_line,ptr,"track",pfc_infinite))
	{
		while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;

		t_size track_base = ptr, track_length;
		while(ptr < p_line_length && !is_spacing(p_line[ptr]))
		{
			if (!is_numeric(p_line[ptr])) pfc::throw_exception_with_message< exception_cue > ("invalid TRACK syntax");
			ptr++;
		}
		track_length = ptr - track_base;
		while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;
		
		t_size type_base = ptr, type_length;
		while(ptr < p_line_length && !is_spacing(p_line[ptr])) ptr++;
		type_length = ptr - type_base;

		while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;
		if (ptr != p_line_length || type_length == 0) pfc::throw_exception_with_message< exception_cue > ("invalid TRACK syntax");
		unsigned track = pfc::atoui_ex(p_line+track_base,track_length);
		if (track < 1 || track > maximumCueTrackNumber) pfc::throw_exception_with_message< exception_cue > ("invalid track number");

		p_callback.on_track(track,p_line + type_base, type_length);
	}
	else if (!stricmp_utf8_ex(p_line,ptr,"index",pfc_infinite))
	{
		while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;

		t_size index_base,index_length, time_base,time_length;
		index_base = ptr;
		while(ptr < p_line_length && !is_spacing(p_line[ptr]))
		{
			if (!is_numeric(p_line[ptr])) pfc::throw_exception_with_message< exception_cue > ("invalid INDEX syntax" );
			ptr++;
		}
		index_length = ptr - index_base;
		
		while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;
		time_base = ptr;
		while(ptr < p_line_length && !is_spacing(p_line[ptr]))
		{
			if (!is_numeric(p_line[ptr]) && p_line[ptr] != ':')
				pfc::throw_exception_with_message< exception_cue > ("invalid INDEX syntax");
			ptr++;
		}
		time_length = ptr - time_base;

		while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;
		
		if (ptr != p_line_length || index_length == 0 || time_length == 0)
			pfc::throw_exception_with_message< exception_cue > ("invalid INDEX syntax");

		unsigned index = pfc::atoui_ex(p_line+index_base,index_length);
		if (index > maximumCueTrackNumber) pfc::throw_exception_with_message< exception_cue > ("invalid INDEX syntax");
		unsigned time = cuesheet_parse_index_time_ticks_e(p_line + time_base,time_length);
		
		p_callback.on_index(index,time);
	}
	else if (!stricmp_utf8_ex(p_line,ptr,"pregap",pfc_infinite))
	{
		while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;

		t_size time_base, time_length;
		time_base = ptr;
		while(ptr < p_line_length && !is_spacing(p_line[ptr]))
		{
			if (!is_numeric(p_line[ptr]) && p_line[ptr] != ':')
				pfc::throw_exception_with_message< exception_cue > ("invalid PREGAP syntax");
			ptr++;
		}
		time_length = ptr - time_base;

		while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;
		
		if (ptr != p_line_length || time_length == 0)
			pfc::throw_exception_with_message< exception_cue > ("invalid PREGAP syntax");

		unsigned time = cuesheet_parse_index_time_ticks_e(p_line + time_base,time_length);
		
		p_callback.on_pregap(time);
	}
	else if (!stricmp_utf8_ex(p_line,ptr,"title",pfc_infinite))
	{
		auto arg = cue_line_argument(p_line+ptr, p_line_length-ptr);
		if ( arg.m_len > 0 ) p_callback.on_title( arg.m_ptr, arg.m_len );
	}
	else if (!stricmp_utf8_ex(p_line,ptr,"performer",pfc_infinite))
	{
		auto arg = cue_line_argument(p_line + ptr, p_line_length - ptr);
		// 2021-01 fix: allow blank performer
		/*if (arg.m_len > 0)*/ p_callback.on_performer(arg.m_ptr, arg.m_len);
	}
	else if (!stricmp_utf8_ex(p_line,ptr,"songwriter",pfc_infinite))
	{
		auto arg = cue_line_argument(p_line + ptr, p_line_length - ptr);
		if (arg.m_len > 0) p_callback.on_songwriter(arg.m_ptr, arg.m_len);
	}
	else if (!stricmp_utf8_ex(p_line,ptr,"isrc",pfc_infinite))
	{
		while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;
		t_size length = p_line_length - ptr;
		if (length == 0) pfc::throw_exception_with_message< exception_cue > ("invalid ISRC syntax");
		p_callback.on_isrc(p_line+ptr,length);
	}
	else if (!stricmp_utf8_ex(p_line,ptr,"catalog",pfc_infinite))
	{
		while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;
		t_size length = p_line_length - ptr;
		if (length == 0) pfc::throw_exception_with_message< exception_cue > ("invalid CATALOG syntax");
		p_callback.on_catalog(p_line+ptr,length);
	}
	else if (!stricmp_utf8_ex(p_line,ptr,"flags",pfc_infinite))
	{
		while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;
		if (ptr < p_line_length)
			p_callback.on_flags(p_line + ptr, p_line_length - ptr);
	}
	else if (!stricmp_utf8_ex(p_line,ptr,"rem",pfc_infinite))
	{
		while(ptr < p_line_length && is_spacing(p_line[ptr])) ptr++;
		if (ptr < p_line_length)
			p_callback.on_comment(p_line + ptr, p_line_length - ptr);
	}
	else if (!stricmp_utf8_ex(p_line,ptr,"postgap",pfc_infinite)) {
		pfc::throw_exception_with_message< exception_cue > ("POSTGAP is not supported");
	} else if (!stricmp_utf8_ex(p_line,ptr,"cdtextfile",pfc_infinite)) {
		//do nothing
	}
	else pfc::throw_exception_with_message< exception_cue > ("unknown cuesheet item");
}

static void g_parse_cue(const char * p_cuesheet,cue_parser_callback & p_callback)
{
	const char * parseptr = p_cuesheet;
	t_size lineidx = 1;
	while(*parseptr)
	{
		while(is_spacing(*parseptr)) parseptr++;
		if (*parseptr)
		{
			t_size length = 0;
			while(parseptr[length] && !is_linebreak(parseptr[length])) length++;
			if (length > 0) {
				try {
					g_parse_cue_line(parseptr,length,p_callback);
				} catch(exception_cue const & e) {//rethrow with line info
					pfc::throw_exception_with_message< exception_cue > (PFC_string_formatter() << e.what() << " (line " << (unsigned)lineidx << ")");
				}
			}
			parseptr += length;
			while(is_linebreak(*parseptr)) {
				if (*parseptr == '\n') lineidx++;
				parseptr++;
			}
		}
	}
}

void cue_parser::parse(const char *p_cuesheet,t_cue_entry_list & p_out) {
	try {
		cue_parser_callback_retrievelist callback(p_out);
		g_parse_cue(p_cuesheet,callback);
		callback.finalize();
	} catch(exception_cue const & e) {
        pfc::throw_exception_with_message<exception_bad_cuesheet>(PFC_string_formatter() << "Error parsing cuesheet: " << e.what());
	}
}
void cue_parser::parse_info(const char * p_cuesheet,file_info & p_info,unsigned p_index) {
	try {
		cue_parser_callback_retrieveinfo callback(p_info,p_index);
		g_parse_cue(p_cuesheet,callback);
		callback.finalize();
	} catch(exception_cue const & e) {
        pfc::throw_exception_with_message< exception_bad_cuesheet > (PFC_string_formatter() << "Error parsing cuesheet: " << e.what());
	}
}

namespace {

	class cue_parser_callback_retrievecount : public cue_parser_callback
	{
	public:
		cue_parser_callback_retrievecount() : m_count(0) {}
		unsigned get_count() const {return m_count;}
		void on_file(const char * p_file,t_size p_file_length,const char * p_type,t_size p_type_length) {}
		void on_track(unsigned p_index,const char * p_type,t_size p_type_length) {m_count++;}
		void on_pregap(unsigned p_value) {}
		void on_index(unsigned p_index,unsigned p_value) {}
		void on_title(const char * p_title,t_size p_title_length) {}
		void on_performer(const char * p_performer,t_size p_performer_length) {}
		void on_isrc(const char * p_isrc,t_size p_isrc_length) {}
		void on_catalog(const char * p_catalog,t_size p_catalog_length) {}
		void on_comment(const char * p_comment,t_size p_comment_length) {}
		void on_flags(const char * p_flags,t_size p_flags_length) {}
	private:
		unsigned m_count;
	};

	class cue_parser_callback_retrievecreatorentries : public cue_parser_callback
	{
	public:
		cue_parser_callback_retrievecreatorentries(cue_creator::t_entry_list & p_out) : m_out(p_out), m_track(0), m_pregap(0), m_index0_set(false), m_index1_set(false) {}

		void on_file(const char * p_file,t_size p_file_length,const char * p_type,t_size p_type_length) {
			validate_file_type(p_type,p_type_length);
			m_file.set_string(p_file,p_file_length);
			m_fileType.set_string(p_type, p_type_length);
		}
		
		void on_track(unsigned p_index,const char * p_type,t_size p_type_length)
		{
			finalize_track();

			m_trackType.set_string( p_type, p_type_length );

			//if (p_index != m_track + 1) throw exception_cue("cuesheet tracks out of order",0);
			
			if (m_file.is_empty()) pfc::throw_exception_with_message< exception_cue > ("declaring a track with no file set");
			m_trackfile = m_file;
			m_trackFileType = m_fileType;
			m_track = p_index;
		}
		
		void on_pregap(unsigned p_value)
		{
			m_pregap = (double) p_value / 75.0;
		}

		void on_index(unsigned p_index,unsigned p_value)
		{
			if (p_index < t_cuesheet_index_list::count)
			{
				switch(p_index)
				{
				case 0:	m_index0_set = true; break;
				case 1: m_index1_set = true; break;
				}
				m_indexes.m_positions[p_index] = (double) p_value / 75.0;
			}
		}
		void on_title(const char * p_title,t_size p_title_length) {}
		void on_performer(const char * p_performer,t_size p_performer_length) {}
		void on_songwriter(const char * p_performer,t_size p_performer_length) {}
		void on_isrc(const char * p_isrc,t_size p_isrc_length) {}
		void on_catalog(const char * p_catalog,t_size p_catalog_length) {}
		void on_comment(const char * p_comment,t_size p_comment_length) {}		
		void finalize()
		{
			finalize_track(); 
		}
		void on_flags(const char * p_flags,t_size p_flags_length) {
			m_flags.set_string(p_flags,p_flags_length);
		}
	private:
		void finalize_track()
		{
			if ( m_track != 0 ) {
				if (m_track < 1 || m_track > maximumCueTrackNumber) pfc::throw_exception_with_message< exception_cue > ("track number out of range");
				if (!m_index1_set) pfc::throw_exception_with_message< exception_cue > ("INDEX 01 not set");
				if (!m_index0_set) m_indexes.m_positions[0] = m_indexes.m_positions[1] - m_pregap;
				if (!m_indexes.is_valid()) pfc::throw_exception_with_message< exception_cue > ("invalid index list");

				cue_creator::t_entry_list::iterator iter;
				iter = m_out.insert_last();
				iter->m_track_number = m_track;
				iter->m_file = m_trackfile;
				iter->m_fileType = m_trackFileType;
				iter->m_index_list = m_indexes;			
				iter->m_flags = m_flags;
				iter->m_trackType = m_trackType;
			}
			m_pregap = 0;
			m_indexes.reset();
			m_index0_set = m_index1_set = false;
			m_flags.reset();
			m_trackType.reset();
		}

		bool m_index0_set,m_index1_set;
		double m_pregap;
		unsigned m_track;
		cue_creator::t_entry_list & m_out;
		pfc::string8 m_file, m_fileType,m_trackfile, m_trackFileType, m_flags, m_trackType;
		t_cuesheet_index_list m_indexes;
	};
}

void cue_parser::parse_full(const char * p_cuesheet,cue_creator::t_entry_list & p_out) {
	try {
		{
			cue_parser_callback_retrievecreatorentries callback(p_out);
			g_parse_cue(p_cuesheet,callback);
			callback.finalize();
		}

		{
			cue_creator::t_entry_list::iterator iter;
			for(iter=p_out.first();iter.is_valid();++iter) {
				if ( iter->isTrackAudio() ) {
					cue_parser_callback_retrieveinfo callback(iter->m_infos,iter->m_track_number);
					g_parse_cue(p_cuesheet,callback);
					callback.finalize();
				}
			}
		}
	} catch(exception_cue const & e) {
        pfc::throw_exception_with_message< exception_bad_cuesheet > (PFC_string_formatter() << "Error parsing cuesheet: " << e.what());
	}
}

namespace file_info_record_helper {
	namespace {
		class __file_info_record__info__enumerator {
		public:
			__file_info_record__info__enumerator(file_info & p_out) : m_out(p_out) {}
			void operator() (const char * p_name, const char * p_value) { m_out.__info_add_unsafe(p_name, p_value); }
		private:
			file_info & m_out;
		};

		class __file_info_record__meta__enumerator {
		public:
			__file_info_record__meta__enumerator(file_info & p_out) : m_out(p_out) {}
			template<typename t_value> void operator() (const char * p_name, const t_value & p_value) {
				t_size index = ~0;
				for (typename t_value::const_iterator iter = p_value.first(); iter.is_valid(); ++iter) {
					if (index == ~0) index = m_out.__meta_add_unsafe(p_name, *iter);
					else m_out.meta_add_value(index, *iter);
				}
			}
		private:
			file_info & m_out;
		};
	}

	void file_info_record::from_info(const file_info & p_info) {
		reset();
		m_length = p_info.get_length();
		m_replaygain = p_info.get_replaygain();
		from_info_overwrite_meta(p_info);
		from_info_overwrite_info(p_info);
	}
	void file_info_record::to_info(file_info & p_info) const {
		p_info.reset();
		p_info.set_length(m_length);
		p_info.set_replaygain(m_replaygain);

		{
			__file_info_record__info__enumerator e(p_info);
			m_info.enumerate(e);
		}
		{
			__file_info_record__meta__enumerator e(p_info);
			m_meta.enumerate(e);
		}
	}

	void file_info_record::reset() {
		m_meta.remove_all(); m_info.remove_all();
		m_length = 0;
		m_replaygain = replaygain_info_invalid;
	}

	void file_info_record::from_info_overwrite_info(const file_info & p_info) {
		for (t_size infowalk = 0, infocount = p_info.info_get_count(); infowalk < infocount; ++infowalk) {
			m_info.set(p_info.info_enum_name(infowalk), p_info.info_enum_value(infowalk));
		}
	}
	void file_info_record::from_info_overwrite_meta(const file_info & p_info) {
		for (t_size metawalk = 0, metacount = p_info.meta_get_count(); metawalk < metacount; ++metawalk) {
			const t_size valuecount = p_info.meta_enum_value_count(metawalk);
			if (valuecount > 0) {
				t_meta_value & entry = m_meta.find_or_add(p_info.meta_enum_name(metawalk));
				entry.remove_all();
				for (t_size valuewalk = 0; valuewalk < valuecount; ++valuewalk) {
					entry.add_item(p_info.meta_enum_value(metawalk, valuewalk));
				}
			}
		}
	}

	void file_info_record::from_info_overwrite_rg(const file_info & p_info) {
		m_replaygain = replaygain_info::g_merge(m_replaygain, p_info.get_replaygain());
	}

	void file_info_record::merge_overwrite(const file_info & p_info) {
		from_info_overwrite_info(p_info);
		from_info_overwrite_meta(p_info);
		from_info_overwrite_rg(p_info);
	}

	void file_info_record::transfer_meta_entry(const char * p_name, const file_info & p_info, t_size p_index) {
		const t_size count = p_info.meta_enum_value_count(p_index);
		if (count == 0) {
			m_meta.remove(p_name);
		} else {
			t_meta_value & val = m_meta.find_or_add(p_name);
			val.remove_all();
			for (t_size walk = 0; walk < count; ++walk) {
				val.add_item(p_info.meta_enum_value(p_index, walk));
			}
		}
	}

	void file_info_record::meta_set(const char * p_name, const char * p_value) {
		m_meta.find_or_add(p_name).set_single(p_value);
	}

	const file_info_record::t_meta_value * file_info_record::meta_query_ptr(const char * p_name) const {
		return m_meta.query_ptr(p_name);
	}
	size_t file_info_record::meta_value_count(const char* name) const {
		auto v = meta_query_ptr(name);
		if (v == nullptr) return 0;
		return v->get_count();
	}


	void file_info_record::from_info_set_meta(const file_info & p_info) {
		m_meta.remove_all();
		from_info_overwrite_meta(p_info);
	}

}