#pragma once

#include <boost/preprocessor/cat.hpp>

template <class Body>
class ScopeGuard {
public:
    explicit ScopeGuard(Body body): body_(body)
    {}

    ~ScopeGuard() { body_(); }

private:
    Body body_;
};

template <class Body>
auto scopeGuard(Body body) { return ScopeGuard<Body>(body); }

#define SCOPE_EXIT(...) auto BOOST_PP_CAT(guard, __LINE__) = scopeGuard([&] { __VA_ARGS__; });
