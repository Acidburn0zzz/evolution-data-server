/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 */

#if !defined (__LIBEDATA_BOOK_H_INSIDE__) && !defined (LIBEDATA_BOOK_COMPILATION)
#error "Only <libedata-book/libedata-book.h> should be included directly."
#endif

#ifndef E_BOOK_BACKEND_SYNC_H
#define E_BOOK_BACKEND_SYNC_H

#include <libedata-book/e-book-backend.h>

/* Standard GObject macros */
#define E_TYPE_BOOK_BACKEND_SYNC \
	(e_book_backend_sync_get_type ())
#define E_BOOK_BACKEND_SYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_BOOK_BACKEND_SYNC, EBookBackendSync))
#define E_BOOK_BACKEND_SYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_BOOK_BACKEND_SYNC, EBookBackendSyncClass))
#define E_IS_BOOK_BACKEND_SYNC(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_BOOK_BACKEND_SYNC))
#define E_IS_BOOK_BACKEND_SYNC_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_BOOK_BACKEND_SYNC))
#define E_BOOK_BACKEND_SYNC_GET_CLASS(cls) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((cls), E_TYPE_BOOK_BACKEND_SYNC, EBookBackendSyncClass))

G_BEGIN_DECLS

typedef struct _EBookBackendSync EBookBackendSync;
typedef struct _EBookBackendSyncClass EBookBackendSyncClass;
typedef struct _EBookBackendSyncPrivate EBookBackendSyncPrivate;

struct _EBookBackendSync {
	EBookBackend parent_object;
	EBookBackendSyncPrivate *priv;
};

struct _EBookBackendSyncClass {
	EBookBackendClass parent_class;

	/* Virtual methods */
	void		(*open_sync)		(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 gboolean only_if_exists,
						 GError **error);
	void		(*refresh_sync)		(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*get_backend_property_sync)
						(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const gchar *prop_name,
						 gchar **prop_value,
						 GError **error);

	/* This method is deprecated. */
	gboolean	(*set_backend_property_sync)
						(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const gchar *prop_name,
						 const gchar *prop_value,
						 GError **error);

	void		(*create_contacts_sync)	(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const GSList *vcards,
						 GSList **added_contacts,
						 GError **error);
	void		(*remove_contacts_sync)	(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const GSList *id_list,
						 GSList **removed_ids,
						 GError **error);
	void		(*modify_contacts_sync)	(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const GSList *vcards,
						 GSList **modified_contacts,
						 GError **error);
	void		(*get_contact_sync)	(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const gchar *id,
						 gchar **vcard,
						 GError **error);
	void		(*get_contact_list_sync)
						(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const gchar *query,
						 GSList **contacts,
						 GError **error);
	void		(*get_contact_list_uids_sync)
						(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const gchar *query,
						 GSList **contacts_uids,
						 GError **error);
};

GType		e_book_backend_sync_get_type	(void) G_GNUC_CONST;

gboolean	e_book_backend_sync_construct	(EBookBackendSync *backend);

void		e_book_backend_sync_open	(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 gboolean only_if_exists,
						 GError **error);
void		e_book_backend_sync_refresh	(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 GError **error);
gboolean	e_book_backend_sync_get_backend_property
						(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const gchar *prop_name,
						 gchar **prop_value,
						 GError **error);
void		e_book_backend_sync_create_contacts
						(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const GSList *vcards,
						 GSList **added_contacts,
						 GError **error);
void		e_book_backend_sync_remove_contacts
						(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const GSList *id_list,
						 GSList **removed_ids,
						 GError **error);
void		e_book_backend_sync_modify_contacts
						(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const GSList *vcards,
						 GSList **modified_contacts,
						 GError **error);
void		e_book_backend_sync_get_contact	(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const gchar *id,
						 gchar **vcard,
						 GError **error);
void		e_book_backend_sync_get_contact_list
						(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const gchar *query,
						 GSList **contacts,
						 GError **error);
void		e_book_backend_sync_get_contact_list_uids
						(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const gchar *query,
						 GSList **contacts_uids,
						 GError **error);

#ifndef EDS_DISABLE_DEPRECATED
gboolean	e_book_backend_sync_set_backend_property
						(EBookBackendSync *backend,
						 EDataBook *book,
						 GCancellable *cancellable,
						 const gchar *prop_name,
						 const gchar *prop_value,
						 GError **error);
#endif /* EDS_DISABLE_DEPRECATED */

G_END_DECLS

#endif /* E_BOOK_BACKEND_SYNC_H */
