/***************************************************************************
 *   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  *
 *                                                                         *
 *   This file is part of LuxRender.                                       *
 *                                                                         *
 *   LuxRender is free software; you can redistribute it and/or modify     *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   any later version.                                                    *
 *                                                                         *
 *   LuxRender is distributed in the hope that it will be useful,          *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program. If not, see <http://www.gnu.org/licenses/>   *
 *                                                                         *
 *   This project is based on PBRT; see <http://www.pbrt.org>              *
 ***************************************************************************/

#ifndef LUX2_QUERYABLE_H
#define LUX2_QUERYABLE_H

#include "core/error.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace lux2
{

    // Attribute data types.
    struct AttributeType
    {
        enum DataType
        {
            None,
            Bool,
            Int,
            Float,
            Double,
            String
        };
    };

    // A single named, typed attribute.
    class QueryableAttribute
    {
    public:
        QueryableAttribute(const std::string &n, const std::string &d)
            : name(n), description(d) {}
        virtual ~QueryableAttribute() = default;

        virtual AttributeType::DataType Type() const { return AttributeType::None; }
        std::string TypeStr() const;

        // String form of the current value.
        virtual std::string Value() const = 0;

        // Typed getters.
        virtual bool BoolValue() const { throw TypeMismatch("bool"); }
        virtual int IntValue() const { throw TypeMismatch("int"); }
        virtual float FloatValue() const { throw TypeMismatch("float"); }
        virtual double DoubleValue() const { throw TypeMismatch("double"); }
        virtual std::string StringValue() const { throw TypeMismatch("string"); }

        // Typed setters.
        virtual void Set(bool) { throw ReadOnly(); }
        virtual void Set(int) { throw ReadOnly(); }
        virtual void Set(float) { throw ReadOnly(); }
        virtual void Set(double) { throw ReadOnly(); }
        virtual void Set(const std::string &) { throw ReadOnly(); }

        virtual bool HasDefaultValue() const { return false; }
        virtual std::string DefaultValue() const { return std::string(); }

        const std::string &Name() const { return name; }
        const std::string &Description() const { return description; }

    protected:
        struct TypeMismatch
        {
            std::string msg;
            explicit TypeMismatch(const char *want)
                : msg(std::string("Queryable attribute type incompatible with '") + want + "'") {}
        };
        struct ReadOnly
        {
            std::string msg;
            ReadOnly() : msg("cannot change read-only Queryable attribute") {}
        };

        std::string name;
        std::string description;
    };

    // Attribute for a missing lookup.
    class NullAttribute : public QueryableAttribute
    {
    public:
        NullAttribute() : QueryableAttribute("null", "null attribute") {}
        AttributeType::DataType Type() const override { return AttributeType::None; }
        std::string Value() const override { return "null"; }
    };

    // Typed attribute.
    template <class D>
    class GenericQueryableAttribute : public QueryableAttribute
    {
    public:
        GenericQueryableAttribute(const std::string &n, const std::string &d)
            : QueryableAttribute(n, d), hasDefaultValue(false) {}
        GenericQueryableAttribute(const std::string &n, const std::string &d, D def)
            : QueryableAttribute(n, d), hasDefaultValue(true), defaultValue(def) {}

        bool HasDefaultValue() const override { return hasDefaultValue; }

        std::function<D()> getFunc;
        std::function<void(D)> setFunc;

    protected:
        bool hasDefaultValue;
        D defaultValue{};
    };

    // Concrete attribute per scalar type.
    template <AttributeType::DataType TYPE, class D>
    class TypedQueryableAttribute : public GenericQueryableAttribute<D>
    {
    public:
        using GenericQueryableAttribute<D>::GenericQueryableAttribute;
        AttributeType::DataType Type() const override { return TYPE; }
    };

    class QueryableBoolAttribute : public TypedQueryableAttribute<AttributeType::Bool, bool>
    {
    public:
        using Base = TypedQueryableAttribute<AttributeType::Bool, bool>;
        using Base::Base;
        std::string Value() const override { return getFunc() ? "true" : "false"; }
        std::string DefaultValue() const override { return hasDefaultValue ? (defaultValue ? "true" : "false") : std::string(); }
        bool BoolValue() const override { return getFunc(); }
        void Set(bool v) override
        {
            if (setFunc)
                setFunc(v);
            else
                throw ReadOnly();
        }
    };

    class QueryableIntAttribute : public TypedQueryableAttribute<AttributeType::Int, int>
    {
    public:
        using Base = TypedQueryableAttribute<AttributeType::Int, int>;
        using Base::Base;
        std::string Value() const override { return std::to_string(getFunc()); }
        std::string DefaultValue() const override { return hasDefaultValue ? std::to_string(defaultValue) : std::string(); }
        int IntValue() const override { return getFunc(); }
        float FloatValue() const override { return static_cast<float>(getFunc()); }
        double DoubleValue() const override { return static_cast<double>(getFunc()); }
        void Set(int v) override
        {
            if (setFunc)
                setFunc(v);
            else
                throw ReadOnly();
        }
    };

    class QueryableFloatAttribute : public TypedQueryableAttribute<AttributeType::Float, float>
    {
    public:
        using Base = TypedQueryableAttribute<AttributeType::Float, float>;
        using Base::Base;
        std::string Value() const override;
        std::string DefaultValue() const override;
        float FloatValue() const override { return getFunc(); }
        double DoubleValue() const override { return static_cast<double>(getFunc()); }
        void Set(float v) override
        {
            if (setFunc)
                setFunc(v);
            else
                throw ReadOnly();
        }
        void Set(double v) override
        {
            if (setFunc)
                setFunc(static_cast<float>(v));
            else
                throw ReadOnly();
        }
    };

    class QueryableDoubleAttribute : public TypedQueryableAttribute<AttributeType::Double, double>
    {
    public:
        using Base = TypedQueryableAttribute<AttributeType::Double, double>;
        using Base::Base;
        std::string Value() const override;
        std::string DefaultValue() const override;
        float FloatValue() const override { return static_cast<float>(getFunc()); }
        double DoubleValue() const override { return getFunc(); }
        void Set(float v) override
        {
            if (setFunc)
                setFunc(static_cast<double>(v));
            else
                throw ReadOnly();
        }
        void Set(double v) override
        {
            if (setFunc)
                setFunc(v);
            else
                throw ReadOnly();
        }
    };

    class QueryableStringAttribute : public TypedQueryableAttribute<AttributeType::String, std::string>
    {
    public:
        using Base = TypedQueryableAttribute<AttributeType::String, std::string>;
        using Base::Base;
        std::string Value() const override { return getFunc(); }
        std::string DefaultValue() const override { return hasDefaultValue ? defaultValue : std::string(); }
        std::string StringValue() const override { return getFunc(); }
        void Set(const std::string &v) override
        {
            if (setFunc)
                setFunc(v);
            else
                throw ReadOnly();
        }
    };

    // Base for any object exposing attributes through the API.
    class Queryable
    {
    public:
        explicit Queryable(std::string n = std::string()) : name(std::move(n)) {}
        virtual ~Queryable() = default;

        const std::string &GetName() const { return name; }

        void AddAttribute(std::unique_ptr<QueryableAttribute> attr)
        {
            attributes[attr->Name()] = std::move(attr);
        }

        bool HasAttribute(const std::string &attributeName) const
        {
            return attributes.find(attributeName) != attributes.end();
        }

        // Returns the attribute, or a shared null attribute on a miss.
        const QueryableAttribute &operator[](const std::string &attributeName) const
        {
            auto it = attributes.find(attributeName);
            if (it != attributes.end())
                return *it->second;
            LOG(LUX_ERROR, LUX_BADTOKEN) << "Attribute '" << attributeName
                                         << "' does not exist in Queryable object";
            return nullAttribute;
        }

        QueryableAttribute &operator[](const std::string &attributeName)
        {
            auto it = attributes.find(attributeName);
            if (it != attributes.end())
                return *it->second;
            LOG(LUX_ERROR, LUX_BADTOKEN) << "Attribute '" << attributeName
                                         << "' does not exist in Queryable object";
            return nullAttribute;
        }

        using Map = std::map<std::string, std::unique_ptr<QueryableAttribute>>;
        Map::const_iterator begin() const { return attributes.begin(); }
        Map::const_iterator end() const { return attributes.end(); }

    private:
        Map attributes;
        std::string name;
        NullAttribute nullAttribute;
    };

    // Unknown tokens expand to the empty string.
    std::string FormatTemplate(const Queryable &obj, const std::string &tpl);

    // ---------------------------------------------------------------------
    // Attribute registration helpers
    // ---------------------------------------------------------------------
    template <class T>
    void AddBoolAttribute(T &object, const std::string &name, const std::string &desc,
                          std::function<bool()> get, std::function<void(bool)> set = nullptr)
    {
        auto a = std::make_unique<QueryableBoolAttribute>(name, desc);
        a->getFunc = std::move(get);
        a->setFunc = std::move(set);
        object.AddAttribute(std::move(a));
    }

    template <class T>
    void AddIntAttribute(T &object, const std::string &name, const std::string &desc,
                         std::function<int()> get, std::function<void(int)> set = nullptr)
    {
        auto a = std::make_unique<QueryableIntAttribute>(name, desc);
        a->getFunc = std::move(get);
        a->setFunc = std::move(set);
        object.AddAttribute(std::move(a));
    }

    template <class T>
    void AddFloatAttribute(T &object, const std::string &name, const std::string &desc,
                           std::function<float()> get, std::function<void(float)> set = nullptr)
    {
        auto a = std::make_unique<QueryableFloatAttribute>(name, desc);
        a->getFunc = std::move(get);
        a->setFunc = std::move(set);
        object.AddAttribute(std::move(a));
    }

    template <class T>
    void AddDoubleAttribute(T &object, const std::string &name, const std::string &desc,
                            std::function<double()> get, std::function<void(double)> set = nullptr)
    {
        auto a = std::make_unique<QueryableDoubleAttribute>(name, desc);
        a->getFunc = std::move(get);
        a->setFunc = std::move(set);
        object.AddAttribute(std::move(a));
    }

    template <class T>
    void AddStringAttribute(T &object, const std::string &name, const std::string &desc,
                            std::function<std::string()> get,
                            std::function<void(const std::string &)> set = nullptr)
    {
        auto a = std::make_unique<QueryableStringAttribute>(name, desc);
        a->getFunc = std::move(get);
        a->setFunc = std::move(set);
        object.AddAttribute(std::move(a));
    }

} // namespace lux2

#endif // LUX2_QUERYABLE_H
