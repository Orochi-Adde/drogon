/**
 *
 *  @file CoroMapper.h
 *  @author An Tao
 *
 *  Copyright 2018, An Tao.  All rights reserved.
 *  https://github.com/an-tao/drogon
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Drogon
 *
 */
#pragma once

#ifdef __cpp_impl_coroutine
#include <drogon/utils/coroutine.h>
#include <drogon/orm/Mapper.h>
namespace drogon
{
namespace orm
{
namespace internal
{
template <typename ReturnType>
struct MapperAwaiter : public CallbackAwaiter<ReturnType>
{
    using MapperFunction =
        std::function<void(std::function<void(ReturnType result)> &&,
                           std::function<void(const std::exception_ptr &)> &&)>;
    MapperAwaiter(MapperFunction &&function) : function_(std::move(function))
    {
    }
    void await_suspend(std::coroutine_handle<> handle)
    {
        function_(
            [handle, this](ReturnType result) {
                this->setValue(std::move(result));
                handle.resume();
            },
            [handle, this](const std::exception_ptr &e) {
                this->setException(e);
                handle.resume();
            });
    }

  private:
    MapperFunction function_;
};
}  // namespace internal

/**
 * @brief This template implements coroutine interfaces of ORM. All the methods
 * of this template are coroutine versions of the synchronous interfaces of the
 * orm::Mapper template.
 *
 * @tparam T The type of the model.
 */
template <typename T>
class CoroMapper : public Mapper<T>
{
  public:
    CoroMapper(const DbClientPtr &client) : Mapper<T>(client)
    {
    }
    using TraitsPKType = typename Mapper<T>::TraitsPKType;
    inline const Task<T> findByPrimaryKey(const TraitsPKType &key)
    {
        if constexpr (!std::is_same<typename T::PrimaryKeyType, void>::value)
        {
            auto lb = [this,
                       key](std::function<void(T)> &&callback,
                            std::function<void(const std::exception_ptr &)>
                                &&errCallback) mutable {
                static_assert(
                    !std::is_same<typename T::PrimaryKeyType, void>::value,
                    "No primary key in the table!");
                static_assert(
                    internal::has_sqlForFindingByPrimaryKey<T>::value,
                    "No function member named sqlForFindingByPrimaryKey, "
                    "please "
                    "make sure that the model class is generated by the latest "
                    "version of drogon_ctl");
                // return findFutureOne(Criteria(T::primaryKeyName, key));
                std::string sql = T::sqlForFindingByPrimaryKey();
                if (this->forUpdate_)
                {
                    sql += " for update";
                }
                this->clear();
                auto binder = *(this->client_) << std::move(sql);
                this->outputPrimeryKeyToBinder(key, binder);

                binder >> [callback = std::move(callback),
                           errCallback](const Result &r) {
                    if (r.size() == 0)
                    {
                        errCallback(std::make_exception_ptr(
                            UnexpectedRows("0 rows found")));
                    }
                    else if (r.size() > 1)
                    {
                        errCallback(std::make_exception_ptr(
                            UnexpectedRows("Found more than one row")));
                    }
                    else
                    {
                        callback(T(r[0]));
                    }
                };
                binder >> std::move(errCallback);
                binder.exec();
            };
            co_return co_await internal::MapperAwaiter<T>(std::move(lb));
        }
        else
        {
            LOG_FATAL << "The table must have a primary key";
            abort();
        }
    }
    inline const Task<std::vector<T>> findAll()
    {
        co_return co_await findBy(Criteria());
    }
    inline const Task<size_t> count(const Criteria &criteria = Criteria())
    {
        auto lb =
            [this, criteria](
                std::function<void(const size_t)> &&callback,
                std::function<void(const std::exception_ptr &)> &&errCallback) {
                std::string sql = "select count(*) from ";
                sql += T::tableName;
                if (criteria)
                {
                    sql += " where ";
                    sql += criteria.criteriaString();
                    sql = this->replaceSqlPlaceHolder(sql, "$?");
                }
                this->clear();
                auto binder = *(this->client_) << std::move(sql);
                if (criteria)
                    criteria.outputArgs(binder);
                binder >> [callback = std::move(callback)](const Result &r) {
                    assert(r.size() == 1);
                    callback(r[0][(Row::SizeType)0].as<size_t>());
                };
                binder >> std::move(errCallback);
            };
        co_return co_await internal::MapperAwaiter<size_t>(std::move(lb));
    }
    inline const Task<T> findOne(const Criteria &criteria)
    {
        auto lb =
            [this, criteria](
                std::function<void(T)> &&callback,
                std::function<void(const std::exception_ptr &)> &&errCallback) {
                std::string sql = "select * from ";
                sql += T::tableName;
                bool hasParameters = false;
                if (criteria)
                {
                    sql += " where ";
                    sql += criteria.criteriaString();
                    hasParameters = true;
                }
                sql.append(this->orderByString_);
                if (this->limit_ > 0)
                {
                    hasParameters = true;
                    sql.append(" limit $?");
                }
                if (this->offset_ > 0)
                {
                    hasParameters = true;
                    sql.append(" offset $?");
                }
                if (hasParameters)
                    sql = this->replaceSqlPlaceHolder(sql, "$?");
                if (this->forUpdate_)
                {
                    sql += " for update";
                }
                auto binder = *(this->client_) << std::move(sql);
                if (criteria)
                    criteria.outputArgs(binder);
                if (this->limit_ > 0)
                    binder << this->limit_;
                if (this->offset_)
                    binder << this->offset_;
                this->clear();
                binder >> [errCallback,
                           callback = std::move(callback)](const Result &r) {
                    if (r.size() == 0)
                    {
                        errCallback(std::make_exception_ptr(
                            UnexpectedRows("0 rows found")));
                    }
                    else if (r.size() > 1)
                    {
                        errCallback(std::make_exception_ptr(
                            UnexpectedRows("Found more than one row")));
                    }
                    else
                    {
                        callback(T(r[0]));
                    }
                };
                binder >> std::move(errCallback);
            };
        co_return co_await internal::MapperAwaiter<T>(std::move(lb));
    }
    inline const Task<std::vector<T>> findBy(const Criteria &criteria)
    {
        auto lb =
            [this, criteria](
                std::function<void(std::vector<T>)> &&callback,
                std::function<void(const std::exception_ptr &)> &&errCallback) {
                std::string sql = "select * from ";
                sql += T::tableName;
                bool hasParameters = false;
                if (criteria)
                {
                    hasParameters = true;
                    sql += " where ";
                    sql += criteria.criteriaString();
                }
                sql.append(this->orderByString_);
                if (this->limit_ > 0)
                {
                    hasParameters = true;
                    sql.append(" limit $?");
                }
                if (this->offset_ > 0)
                {
                    hasParameters = true;
                    sql.append(" offset $?");
                }
                if (hasParameters)
                    sql = this->replaceSqlPlaceHolder(sql, "$?");
                if (this->forUpdate_)
                {
                    sql += " for update";
                }
                auto binder = *(this->client_) << std::move(sql);
                if (criteria)
                    criteria.outputArgs(binder);
                if (this->limit_ > 0)
                    binder << this->limit_;
                if (this->offset_)
                    binder << this->offset_;
                this->clear();
                binder >> [callback = std::move(callback)](const Result &r) {
                    std::vector<T> ret;
                    for (auto const &row : r)
                    {
                        ret.push_back(T(row));
                    }
                    callback(ret);
                };
                binder >> std::move(errCallback);
            };
        co_return co_await internal::MapperAwaiter<std::vector<T>>(
            std::move(lb));
    }
    inline const Task<T> insert(const T &obj)
    {
        auto lb =
            [this, obj](
                std::function<void(T)> &&callback,
                std::function<void(const std::exception_ptr &)> &&errCallback) {
                this->clear();
                bool needSelection = false;
                auto binder = *(this->client_)
                              << obj.sqlForInserting(needSelection);
                obj.outputArgs(binder);
                auto client = this->client_;
                binder >> [client,
                           callback = std::move(callback),
                           obj,
                           needSelection,
                           errCallback](const Result &r) {
                    assert(r.affectedRows() == 1);
                    if (client->type() == ClientType::PostgreSQL)
                    {
                        if (needSelection)
                        {
                            assert(r.size() == 1);
                            callback(T(r[0]));
                        }
                        else
                        {
                            callback(obj);
                        }
                    }
                    else  // Mysql or Sqlite3
                    {
                        auto id = r.insertId();
                        auto newObj = obj;
                        newObj.updateId(id);
                        if (needSelection)
                        {
                            auto tmp = Mapper<T>(client);
                            tmp.findByPrimaryKey(
                                newObj.getPrimaryKey(),
                                callback,
                                [errCallback](const DrogonDbException &err) {
                                    errCallback(std::make_exception_ptr(
                                        Failure(err.base().what())));
                                });
                        }
                        else
                        {
                            callback(newObj);
                        }
                    }
                };
                binder >> std::move(errCallback);
            };
        co_return co_await internal::MapperAwaiter<T>(std::move(lb));
    }
    inline const Task<size_t> update(const T &obj)
    {
        auto lb =
            [this, obj](
                std::function<void(const size_t)> &&callback,
                std::function<void(const std::exception_ptr &)> &&errCallback) {
                this->clear();
                static_assert(
                    !std::is_same<typename T::PrimaryKeyType, void>::value,
                    "No primary key in the table!");
                std::string sql = "update ";
                sql += T::tableName;
                sql += " set ";
                for (auto const &colName : obj.updateColumns())
                {
                    sql += colName;
                    sql += " = $?,";
                }
                sql[sql.length() - 1] = ' ';  // Replace the last ','

                this->makePrimaryKeyCriteria(sql);

                sql = this->replaceSqlPlaceHolder(sql, "$?");
                auto binder = *(this->client_) << std::move(sql);
                obj.updateArgs(binder);
                this->outputPrimeryKeyToBinder(obj.getPrimaryKey(), binder);
                binder >> [callback = std::move(callback)](const Result &r) {
                    callback(r.affectedRows());
                };
                binder >> std::move(errCallback);
            };
        co_return co_await internal::MapperAwaiter<size_t>(std::move(lb));
    }
    inline const Task<size_t> deleteOne(const T &obj)
    {
        auto lb =
            [this, obj](
                std::function<void(const size_t)> &&callback,
                std::function<void(const std::exception_ptr &)> &&errCallback) {
                this->clear();
                static_assert(
                    !std::is_same<typename T::PrimaryKeyType, void>::value,
                    "No primary key in the table!");
                std::string sql = "delete from ";
                sql += T::tableName;
                sql += " ";

                this->makePrimaryKeyCriteria(sql);

                sql = this->replaceSqlPlaceHolder(sql, "$?");
                auto binder = *(this->client_) << std::move(sql);
                this->outputPrimeryKeyToBinder(obj.getPrimaryKey(), binder);
                binder >> [callback = std::move(callback)](const Result &r) {
                    callback(r.affectedRows());
                };
                binder >> std::move(errCallback);
            };
        co_return co_await internal::MapperAwaiter<size_t>(std::move(lb));
    }
    inline const Task<size_t> deleteBy(const Criteria &criteria)
    {
        auto lb =
            [this, criteria](
                std::function<void(const size_t)> &&callback,
                std::function<void(const std::exception_ptr &)> &&errCallback) {
                this->clear();
                static_assert(
                    !std::is_same<typename T::PrimaryKeyType, void>::value,
                    "No primary key in the table!");
                std::string sql = "delete from ";
                sql += T::tableName;

                if (criteria)
                {
                    sql += " where ";
                    sql += criteria.criteriaString();
                    sql = this->replaceSqlPlaceHolder(sql, "$?");
                }

                auto binder = *(this->client_) << std::move(sql);
                if (criteria)
                {
                    criteria.outputArgs(binder);
                }
                binder >> [callback = std::move(callback)](const Result &r) {
                    callback(r.affectedRows());
                };
                binder >> std::move(errCallback);
            };
        co_return co_await internal::MapperAwaiter<size_t>(std::move(lb));
    }
    inline const Task<size_t> deleteByPrimaryKey(const TraitsPKType &key)
    {
        static_assert(!std::is_same<typename T::PrimaryKeyType, void>::value,
                      "No primary key in the table!");
        static_assert(
            internal::has_sqlForDeletingByPrimaryKey<T>::value,
            "No function member named sqlForDeletingByPrimaryKey, please "
            "make sure that the model class is generated by the latest "
            "version of drogon_ctl");
        auto lb = [this, key](std::function<void(const size_t)> &&callback,
                              std::function<void(const std::exception_ptr &)>
                                  &&errCallback) {
            this->clear();
            auto binder = *(this->client_) << T::sqlForDeletingByPrimaryKey();
            this->outputPrimeryKeyToBinder(key, binder);
            binder >> [callback = std::move(callback)](const Result &r) {
                callback(r.affectedRows());
            };
            binder >> std::move(errCallback);
        };
        co_return co_await internal::MapperAwaiter<size_t>(std::move(lb));
    }
};
}  // namespace orm
}  // namespace drogon
#endif