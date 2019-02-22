#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "catalog/catalog_defs.h"
#include "catalog/catalog_sql_table.h"
#include "catalog/database_handle.h"
#include "catalog/tablespace_handle.h"
#include "common/strong_typedef.h"
#include "loggers/catalog_logger.h"
#include "storage/sql_table.h"

namespace terrier::catalog {

class DatabaseHandle;
class TablespaceHandle;

/**
 * The global catalog object. It contains all the information about global catalog tables. It's also
 * the entry point for transactions to access any data in any sql table.
 *
 * OID assignment:
 * Note that we do not have a concept of oid_t anymore. Instead, we have
 *  db_oid_t, namespace_oid_t, table_oid_t, col_oid_t
 * In addition, for namespace_oid_t, table_oid_t, and col_oid_t, we only guarantee uniqueness inside a database,
 * which means that the table_oid for pg_attribute in database A could be the same as pg_attribute in database B.
 *
 * db_oid_t, namespace_oid_t, table_oid_t, col_oid_t come from the same global counter, so, inside a database, the
 * values of oids should never be the same.
 *
 * TODO(yangjuns): Each database should have its own global counter
 */
class Catalog {
 public:
  /**
   * Creates the (global) catalog object, and bootstraps, i.e. creates
   * all the default and system databases and tables.
   * @param txn_manager the global transaction manager
   */
  explicit Catalog(transaction::TransactionManager *txn_manager);

  /**
   * Lookup a database oid and return a database handle.
   * @return the corresponding database handle
   */
  DatabaseHandle GetDatabaseHandle();

  /**
   * Return a tablespace handle.
   * @return the tablespace handle
   */
  TablespaceHandle GetTablespaceHandle();

  /**
   * Get the pointer to a catalog in a database by db_oid, including global catalogs.
   *
   * @param db_oid the database the catalog belongs to
   * @param table_oid the table oid of the catalog
   * @return a pointer to the catalog
   * @throw out_of_range exception if either oid doesn't exist or the catalog doesn't exist.
   */
  std::shared_ptr<catalog::SqlTableRW> GetDatabaseCatalog(db_oid_t db_oid, table_oid_t table_oid);

  /**
   * Get the pointer to a catalog in a database by name, including global catalogs.
   *
   * @param db_oid the database the catalog belongs to
   * @param table_name the name of the catalog
   * @return a pointer to the catalog
   * @throw out_of_range exception if either oid doesn't exist or the catalog doesn't exist.
   */
  std::shared_ptr<catalog::SqlTableRW> GetDatabaseCatalog(db_oid_t db_oid, const std::string &table_name);

  /**
   * The global counter for getting next oid. The return result should be converted into corresponding oid type
   *
   * This function is atomic.
   *
   * @return uint32_t the next oid available
   */
  uint32_t GetNextOid();

  /*
   * Destructor
   */
  ~Catalog() {
    // destroy all DB
    DestroyDB(DEFAULT_DATABASE_OID);
  }

 private:
  struct UnusedSchemaCols {
    int32_t col_num;
    const char *col_name;
    type::TypeId type_id;
  };

  /**
   * Add columns created for Postgres compatibility, but unused, to the schema
   * @param db_p - shared_ptr to database
   * @param cols - vector specifying the columns
   *
   */
  void AddUnusedSchemaColumns(const std::shared_ptr<catalog::SqlTableRW> &db_p,
                              const std::vector<UnusedSchemaCols> &cols);

  /**
   * Set values for unused columns.
   * @param db_p - shared_ptr to database
   * @param cols - vector specifying the columns
   */
  void SetUnusedSchemaColumns(const std::shared_ptr<catalog::SqlTableRW> &db_p,
                              const std::vector<UnusedSchemaCols> &cols);

  /**
   * Utility function for adding columns in a table to pg_attribute. To use this function, pg_attribute has to exist.
   * @param txn the transaction that's adding the columns
   * @param db_oid the database the pg_attribute belongs to
   * @param table the table which the columns belong to
   */
  void AddColumnsToPGAttribute(transaction::TransactionContext *txn, db_oid_t db_oid,
                               std::shared_ptr<storage::SqlTable> table);
  /**
   * Bootstrap all the catalog tables so that new coming transactions can
   * correctly perform SQL queries.
   * 1) It creates and populates all the global catalogs
   * 2) It creates a default database named "terrier"
   * 2) It bootstraps the default database.
   */
  void Bootstrap();

  void CreatePGDatabase(table_oid_t table_oid);

  void CreatePGTablespace(table_oid_t table_oid);

  /**
   * Bootstrap a database, i.e create all the catalogs local to this database, and do all other initialization.
   * 1) Create pg_namespace (catalog)
   * 2) Create pg_class (catalog)
   * 3) TODO(pakhtar) -  other catalogs for Postgres compatibility
   * 4) populates these catalogs
   * @param db_oid the oid of the database you are trying to bootstrap
   *
   * Notes:
   * 1) Caller must add the database to pg_database.
   */
  void BootstrapDatabase(transaction::TransactionContext *txn, db_oid_t db_oid);

  /**
   * A dummy call back function for committing bootstrap transaction
   */
  static void BootstrapCallback(void * /*unused*/) {}

  void PopulatePGDatabase(transaction::TransactionContext *txn);

  void PopulatePGTablespace(transaction::TransactionContext *txn);

  void CreatePGNameSpace(transaction::TransactionContext *txn, db_oid_t db_oid);

  void CreatePGClass(transaction::TransactionContext *txn, db_oid_t db_oid);

  void CreatePGAttribute(transaction::TransactionContext *txn, db_oid_t db_oid);

  void DestroyDB(db_oid_t oid);

 private:
  transaction::TransactionManager *txn_manager_;
  // global catalogs
  std::shared_ptr<catalog::SqlTableRW> pg_database_;
  std::shared_ptr<catalog::SqlTableRW> pg_tablespace_;

  // map from (db_oid, catalog table_oid_t) to sql table rw wrapper
  std::unordered_map<db_oid_t, std::unordered_map<table_oid_t, std::shared_ptr<catalog::SqlTableRW>>> map_;
  // map from (db_oid, catalog name) to sql table
  std::unordered_map<db_oid_t, std::unordered_map<std::string, table_oid_t>> name_map_;
  // this oid serves as a global counter for different strong types of oid
  std::atomic<uint32_t> oid_;

  /**
   * pg_database specific items. Should be in a pg_database util class
   */
  // unused column spec for pg_database
  std::vector<UnusedSchemaCols> pg_database_unused_cols_ = {
      {2, "datdba", type::TypeId::INTEGER},        {3, "encoding", type::TypeId::INTEGER},
      {4, "datcollate", type::TypeId::VARCHAR},    {5, "datctype", type::TypeId::VARCHAR},
      {6, "datistemplate", type::TypeId::BOOLEAN}, {7, "datallowconn", type::TypeId::BOOLEAN},
      {8, "datconnlimit", type::TypeId::INTEGER}};
  std::vector<UnusedSchemaCols> pg_tablespace_unused_cols_ = {{2, "spcowner", type::TypeId::INTEGER},
                                                              {3, "spcacl", type::TypeId::VARCHAR},
                                                              {4, "spcoptions", type::TypeId::VARCHAR}};
  std::vector<UnusedSchemaCols> pg_namespace_unused_cols_ = {
      {2, "nspowner", type::TypeId::INTEGER},
      {3, "nspacl", type::TypeId::VARCHAR},
  };
  // TODO(yeshengm): unused column for pg_class. Not implemented now due to __ptr in our pg_class,
  //                 which breaks the numbering of columns as in postgres.
};

extern std::shared_ptr<Catalog> terrier_catalog;

}  // namespace terrier::catalog
