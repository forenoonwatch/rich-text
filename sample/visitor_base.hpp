#pragma once

template<class... Ts> struct VisitorBase : Ts... { using Ts::operator()...; };
template<class... Ts> VisitorBase(Ts...) -> VisitorBase<Ts...>;

