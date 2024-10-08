#pragma once

namespace file_list_helper
{
	typedef pfc::list_base_const_t<const char*> base_t;

	//list guaranteed to be sorted by metadb::path_compare
	class file_list_from_metadb_handle_list : public base_t {
	public:
		file_list_from_metadb_handle_list() {}
		file_list_from_metadb_handle_list( metadb_handle_list_cref lst, bool bDisplayPaths = false );

		static t_size g_get_count(const list_base_const_t<metadb_handle_ptr> & p_list, t_size max = SIZE_MAX);

		void init_from_list(const list_base_const_t<metadb_handle_ptr> & p_list);
		void init_from_list_display(const list_base_const_t<metadb_handle_ptr> & p_list);

		t_size get_count() const;
		void get_item_ex(const char * & p_out,t_size n) const;

		~file_list_from_metadb_handle_list();

	private:
		void _add(const char * p_what);
		pfc::ptr_list_t<char> m_data;
	};
};

