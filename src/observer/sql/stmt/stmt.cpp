/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2022/5/22.
//

#include "sql/stmt/stmt.h"
#include "common/log/log.h"
#include "sql/stmt/analyze_table_stmt.h"
#include "sql/stmt/calc_stmt.h"
#include "sql/stmt/create_index_stmt.h"
#include "sql/stmt/create_table_stmt.h"
#include "sql/stmt/delete_stmt.h"
#include "sql/stmt/drop_table_stmt.h"
#include "sql/stmt/drop_index_stmt.h"
#include "sql/stmt/update_stmt.h"
#include "sql/stmt/union_stmt.h"
#include "sql/stmt/alter_table_stmt.h"
#include "sql/stmt/view_ddl_stmt.h"
#include "sql/stmt/desc_table_stmt.h"
#include "sql/stmt/exit_stmt.h"
#include "sql/stmt/explain_stmt.h"
#include "sql/stmt/help_stmt.h"
#include "sql/stmt/insert_stmt.h"
#include "sql/stmt/load_data_stmt.h"
#include "sql/stmt/select_stmt.h"
#include "sql/stmt/set_variable_stmt.h"
#include "sql/stmt/show_tables_stmt.h"
#include "sql/stmt/trx_begin_stmt.h"
#include "sql/stmt/trx_end_stmt.h"

bool stmt_type_ddl(StmtType type)
{
  switch (type) {
    case StmtType::CREATE_TABLE:
    case StmtType::CREATE_VIEW:
    case StmtType::DROP_VIEW:
    case StmtType::ALTER_TABLE:
    case StmtType::DROP_TABLE:
    case StmtType::DROP_INDEX:
    case StmtType::CREATE_INDEX: {
      return true;
    }
    default: {
      return false;
    }
  }
}
RC Stmt::create_stmt(Db *db, ParsedSqlNode &sql_node, Stmt *&stmt)
{
  stmt = nullptr;

  switch (sql_node.flag) {
    case SCF_INSERT: {
      return InsertStmt::create(db, sql_node.insertion, stmt);
    }
    case SCF_DELETE: {
      return DeleteStmt::create(db, sql_node.deletion, stmt);
    }
    case SCF_UPDATE: {
      return UpdateStmt::create(db, sql_node.update, stmt);
    }
    case SCF_CREATE_VIEW:
    case SCF_DROP_VIEW: {
      const auto type = sql_node.flag == SCF_CREATE_VIEW ? StmtType::CREATE_VIEW : StmtType::DROP_VIEW;
      stmt = new ViewDdlStmt(type, std::move(sql_node.create_view));
      return RC::SUCCESS;
    }
    case SCF_ALTER_TABLE: {
      stmt = new AlterTableStmt(sql_node.alter_table);
      return RC::SUCCESS;
    }
    case SCF_DROP_TABLE: {
      stmt = new DropTableStmt(sql_node.drop_table.relation_name);
      return RC::SUCCESS;
    }
    case SCF_UNION: {
      Stmt *left = nullptr;
      RC rc = create_stmt(db, *sql_node.union_left, left);
      if (OB_FAIL(rc)) {
        return rc;
      }
      unique_ptr<Stmt> first(left);
      Stmt *right = nullptr;
      rc = create_stmt(db, *sql_node.union_right, right);
      if (OB_FAIL(rc)) {
        return rc;
      }
      stmt = new UnionStmt(std::move(first), unique_ptr<Stmt>(right), sql_node.union_all);
      return RC::SUCCESS;
    }
    case SCF_SELECT: {
      return SelectStmt::create(db, sql_node.selection, stmt);
    }

    case SCF_EXPLAIN: {
      return ExplainStmt::create(db, sql_node.explain, stmt);
    }

    case SCF_DROP_INDEX: {
      stmt = new DropIndexStmt(sql_node.drop_index);
      return RC::SUCCESS;
    }
    case SCF_CREATE_INDEX: {
      return CreateIndexStmt::create(db, sql_node.create_index, stmt);
    }

    case SCF_CREATE_TABLE: {
      return CreateTableStmt::create(db, sql_node.create_table, stmt);
    }

    case SCF_DESC_TABLE: {
      return DescTableStmt::create(db, sql_node.desc_table, stmt);
    }

    case SCF_ANALYZE_TABLE: { 
      return AnalyzeTableStmt::create(db, sql_node.analyze_table, stmt);
    }

    case SCF_HELP: {
      return HelpStmt::create(stmt);
    }

    case SCF_SHOW_TABLES: {
      return ShowTablesStmt::create(db, stmt);
    }

    case SCF_BEGIN: {
      return TrxBeginStmt::create(stmt);
    }

    case SCF_COMMIT:
    case SCF_ROLLBACK: {
      return TrxEndStmt::create(sql_node.flag, stmt);
    }

    case SCF_EXIT: {
      return ExitStmt::create(stmt);
    }

    case SCF_SET_VARIABLE: {
      return SetVariableStmt::create(sql_node.set_variable, stmt);
    }

    case SCF_LOAD_DATA: {
      return LoadDataStmt::create(db, sql_node.load_data, stmt);
    }

    case SCF_CALC: {
      return CalcStmt::create(sql_node.calc, stmt);
    }

    default: {
      LOG_INFO("Command::type %d doesn't need to create statement.", sql_node.flag);
    } break;
  }
  return RC::UNIMPLEMENTED;
}
