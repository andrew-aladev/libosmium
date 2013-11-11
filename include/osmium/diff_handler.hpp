#ifndef OSMIUM_DIFF_HANDLER_HPP
#define OSMIUM_DIFF_HANDLER_HPP

/*

This file is part of Osmium (http://osmcode.org/osmium).

Copyright 2013 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <osmium/memory/iterator.hpp>
#include <osmium/osm/diff_object.hpp>

namespace osmium {

    namespace diff_handler {

        class DiffHandler {

        public:

            DiffHandler() {
            }

            void node(const osmium::DiffNode&) const {
            }

            void way(const osmium::DiffWay&) const {
            }

            void relation(const osmium::DiffRelation&) const {
            }

            void init() const {
            }

            void before_nodes() const {
            }

            void after_nodes() const {
            }

            void before_ways() const {
            }

            void after_ways() const {
            }

            void before_relations() const {
            }

            void after_relations() const {
            }

            void before_changesets() const {
            }

            void after_changesets() const {
            }

            void done() const {
            }

        }; // class DiffHandler

        namespace detail {

            template <class THandler>
            inline void apply_before_and_after_recurse(osmium::item_type last, osmium::item_type current, THandler& handler) {
                switch (last) {
                    case osmium::item_type::undefined:
                        handler.init();
                        break;
                    case osmium::item_type::node:
                        handler.after_nodes();
                        break;
                    case osmium::item_type::way:
                        handler.after_ways();
                        break;
                    case osmium::item_type::relation:
                        handler.after_relations();
                        break;
                    default:
                        break;
                }
                switch (current) {
                    case osmium::item_type::undefined:
                        handler.done();
                        break;
                    case osmium::item_type::node:
                        handler.before_nodes();
                        break;
                    case osmium::item_type::way:
                        handler.before_ways();
                        break;
                    case osmium::item_type::relation:
                        handler.before_relations();
                        break;
                    default:
                        break;
                }
            }

            template <class THandler, class ...TRest>
            inline void apply_before_and_after_recurse(osmium::item_type last, osmium::item_type current, THandler& handler, TRest&... more) {
                apply_before_and_after_recurse(last, current, handler);
                apply_before_and_after_recurse(last, current, more...);
            }

            template <class TIterator, class THandler>
            inline void apply_item_recurse(TIterator prev, TIterator it, TIterator next, THandler& handler) {
                if (prev->type() != it->type() || prev->id() != it->id()) {
                    prev = it;
                }

                if (next->type() != it->type() || next->id() != it->id()) {
                    next = it;
                }

                switch (it->type()) {
                    case osmium::item_type::node:
                        handler.node(DiffNode{ static_cast<const osmium::Node&>(*prev), static_cast<const osmium::Node&>(*it), static_cast<const osmium::Node&>(*next) });
                        break;
                    case osmium::item_type::way:
                        handler.way(DiffWay{ static_cast<const osmium::Way&>(*prev), static_cast<const osmium::Way&>(*it), static_cast<const osmium::Way&>(*next) });
                        break;
                    case osmium::item_type::relation:
                        handler.relation(DiffRelation{ static_cast<const osmium::Relation&>(*prev), static_cast<const osmium::Relation&>(*it), static_cast<const osmium::Relation&>(*next) });
                        break;
                    default:
                        throw std::runtime_error("unknown type");
                }
            }

            template <class TIterator, class THandler, class ...TRest>
            inline void apply_item_recurse(TIterator prev, TIterator it, TIterator next, THandler& handler, TRest&... more) {
                apply_item_recurse(prev, it, next, handler);
                apply_item_recurse(prev, it, next, more...);
            }

        } // namespace detail

        template <class TIterator, class ...THandlers>
        inline void apply(TIterator it, TIterator end, THandlers&... handlers) {
            osmium::item_type last_type = osmium::item_type::undefined;
            TIterator prev = it;
            TIterator next = it;
            while (it != end) {

                if (last_type != it->type()) {
                    osmium::diff_handler::detail::apply_before_and_after_recurse(last_type, it->type(), handlers...);
                    last_type = it->type();
                }

                ++next;
                if (next == end) {
                    osmium::diff_handler::detail::apply_item_recurse(prev, it, it, handlers...);
                    osmium::diff_handler::detail::apply_before_and_after_recurse(last_type, osmium::item_type::undefined, handlers...);
                    return;
                }

                osmium::diff_handler::detail::apply_item_recurse(prev, it, next, handlers...);

                prev = it;
                it = next;
            }
        }

        template <class TSource, class ...THandlers>
        inline void apply(TSource& source, THandlers&... handlers) {
            apply(osmium::memory::Iterator<TSource, osmium::Object>{source},
                  osmium::memory::Iterator<TSource, osmium::Object>{},
                  handlers...);
        }

        template <class ...THandlers>
        inline void apply(const osmium::memory::Buffer& buffer, THandlers&... handlers) {
            apply(buffer.begin(), buffer.end(), handlers...);
        }

    } // namespace diff_handler

} // namespace osmium

#endif // OSMIUM_DIFF_HANDLER_HPP