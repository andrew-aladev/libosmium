#ifndef OSMIUM_AREA_ASSEMBLER_HPP
#define OSMIUM_AREA_ASSEMBLER_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2014 Jochen Topf <jochen@topf.org> and others (see README).

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

#include <algorithm>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <vector>

#include <osmium/memory/buffer.hpp>
#include <osmium/osm/area.hpp>
#include <osmium/osm/builder.hpp>
#include <osmium/osm/location.hpp>
#include <osmium/osm/ostream.hpp>
#include <osmium/osm/relation.hpp>
#include <osmium/tags/key_filter.hpp>

#include <osmium/area/detail/proto_ring.hpp>
#include <osmium/area/detail/segment_list.hpp>
#include <osmium/area/problem_reporter.hpp>
#include <osmium/area/segment.hpp>

namespace osmium {

    namespace area {

        using osmium::area::detail::ProtoRing;

        /**
         * Assembles area objects from multipolygon relations and their
         * members. This is called by the Collector object after all
         * members have been collected.
         */
        class Assembler {

            osmium::area::ProblemReporter* m_problem_reporter;

            // Enables debug output to stderr
            bool m_debug { false };

            // The way segments
            osmium::area::detail::SegmentList m_segment_list;

            // The rings we are building from the way segments
            std::list<ProtoRing> m_rings;

            // ID of the relation/way we are currently working on
            osmium::object_id_type m_object_id;

            std::vector<ProtoRing*> m_outer_rings;
            std::vector<ProtoRing*> m_inner_rings;

            int m_inner_outer_mismatches { 0 };

            /**
             * Checks whether the given NodeRefs have the same location.
             * Uses the actual location for the test, not the id. If both
             * have the same location, but not the same id, a problem
             * point will be added to the list of problem points.
             */
            bool has_same_location(const osmium::NodeRef& nr1, const osmium::NodeRef& nr2) {
                if (nr1.location() != nr2.location()) {
                    return false;
                }
                if (nr1.ref() != nr2.ref()) {
                    if (m_problem_reporter) {
                        m_problem_reporter->report_duplicate_node(nr1.ref(), nr2.ref(), nr1.location());
                    }
                }
                return true;
            }

            /**
             * Find intersection between segments.
             *
             * @returns true if there are intersections.
             */
            bool find_intersections() {
                if (m_segment_list.empty()) {
                    return false;
                }

                bool found_intersections = false;

                for (auto it1 = m_segment_list.begin(); it1 != m_segment_list.end()-1; ++it1) {
                    const NodeRefSegment& s1 = *it1;
                    for (auto it2 = it1+1; it2 != m_segment_list.end(); ++it2) {
                        const NodeRefSegment& s2 = *it2;
                        if (s1 == s2) {
                            if (m_debug) {
                                std::cerr << "  found overlap on segment " << s1 << "\n";
                            }
                        } else {
                            if (outside_x_range(s2, s1)) {
                                break;
                            }
                            if (y_range_overlap(s1, s2)) {
                                osmium::Location intersection = calculate_intersection(s1, s2);
                                if (intersection) {
                                    found_intersections = true;
                                    if (m_debug) {
                                        std::cerr << "  segments " << s1 << " and " << s2 << " intersecting at " << intersection << "\n";
                                    }
                                    if (m_problem_reporter) {
                                        m_problem_reporter->report_intersection(m_object_id, s1.way()->id(), s1.first().location(), s1.second().location(), s2.way()->id(), s2.first().location(), s2.second().location(), intersection);
                                    }
                                }
                            }
                        }
                    }
                }

                return found_intersections;
            }

            /**
             * Initialize area attributes and tags from the attributes and tags
             * of the given object.
             */
            void initialize_area_from_object(osmium::osm::AreaBuilder& builder, const osmium::Object& object, int id_offset) const {
                osmium::Area& area = builder.object();
                area.id(object.id() * 2 + id_offset);
                area.version(object.version());
                area.changeset(object.changeset());
                area.timestamp(object.timestamp());
                area.visible(object.visible());
                area.uid(object.uid());

                builder.add_user(object.user());
            }

            void add_tags_to_area(osmium::osm::AreaBuilder& builder, const osmium::Way& way) const {
                osmium::osm::TagListBuilder tl_builder(builder.buffer(), &builder);
                for (const osmium::Tag& tag : way.tags()) {
                    tl_builder.add_tag(tag.key(), tag.value());
                }
            }

            void add_common_tags(osmium::osm::TagListBuilder& tl_builder, std::set<const osmium::Way*>& ways) const {
                std::map<std::string, size_t> counter;
                for (const osmium::Way* way : ways) {
                    for (auto& tag : way->tags()) {
                        std::string kv {tag.key()};
                        kv.append(1, '\0');
                        kv.append(tag.value());
                        ++counter[kv];
                    }
                }

                size_t num_ways = ways.size();
                for (auto& t_c : counter) {
                    if (m_debug) {
                        std::cerr << "        tag " << t_c.first << " is used " << t_c.second << " times in " << num_ways << " ways\n";
                    }
                    if (t_c.second == num_ways) {
                        size_t len = std::strlen(t_c.first.c_str());
                        tl_builder.add_tag(t_c.first.c_str(), t_c.first.c_str() + len + 1);
                    }
                }
            }

            void add_tags_to_area(osmium::osm::AreaBuilder& builder, const osmium::Relation& relation) const {
                osmium::tags::KeyFilter filter(true);
                filter.add(false, "type").add(false, "created_by").add(false, "source").add(false, "note");
                filter.add(false, "test:id").add(false, "test:section");

                osmium::tags::KeyFilter::iterator fi_begin(filter, relation.tags().begin(), relation.tags().end());
                osmium::tags::KeyFilter::iterator fi_end(filter, relation.tags().end(), relation.tags().end());

                size_t count = std::distance(fi_begin, fi_end);

                if (m_debug) {
                    std::cerr << "  found " << count << " tags on relation (without ignored ones)\n";
                }

                if (count > 0) {
                    if (m_debug) {
                        std::cerr << "    use tags from relation\n";
                    }

                    // write out all tags except type=*
                    osmium::osm::TagListBuilder tl_builder(builder.buffer(), &builder);
                    for (const osmium::Tag& tag : relation.tags()) {
                        if (strcmp(tag.key(), "type")) {
                            tl_builder.add_tag(tag.key(), tag.value());
                        }
                    }
                } else {
                    if (m_debug) {
                        std::cerr << "    use tags from outer ways\n";
                    }
                    std::set<const osmium::Way*> ways;
                    for (auto& ring : m_outer_rings) {
                        ring->get_ways(ways);
                    }
                    if (ways.size() == 1) {
                        if (m_debug) {
                            std::cerr << "      only one outer way\n";
                        }
                        osmium::osm::TagListBuilder tl_builder(builder.buffer(), &builder);
                        for (const osmium::Tag& tag : (*ways.begin())->tags()) {
                            tl_builder.add_tag(tag.key(), tag.value());
                        }
                    } else {
                        if (m_debug) {
                            std::cerr << "      multiple outer ways, get common tags\n";
                        }
                        osmium::osm::TagListBuilder tl_builder(builder.buffer(), &builder);
                        add_common_tags(tl_builder, ways);
                    }
                }
            }

            /**
             * Go through all the rings and find rings that are not closed.
             * Problem objects are created for the end points of the open
             * rings and placed into the m_problems collection.
             *
             * @returns true if any rings were not closed, false otherwise
             */
            bool check_for_open_rings() {
                bool open_rings = false;

                for (auto& ring : m_rings) {
                    if (!ring.closed()) {
                        open_rings = true;
                        if (m_problem_reporter) {
                            m_problem_reporter->report_ring_not_closed(m_object_id, ring.first_segment().first().location(), ring.last_segment().second().location());
                        }
                    }
                }

                return open_rings;
            }

            /**
             * Check whether there are any rings that can be combined with the
             * given ring to one larger ring by appending the other ring to
             * the end of this ring.
             * If the rings can be combined they are and the function returns
             * true.
             */
            bool possibly_combine_rings_end(ProtoRing& ring) {
                const osmium::NodeRef& nr = ring.last_segment().second();

                if (m_debug) {
                    std::cerr << "      combine_rings_end\n";
                }
                for (auto it = m_rings.begin(); it != m_rings.end(); ++it) {
                    if (&*it != &ring && !it->closed()) {
                        if (has_same_location(nr, it->first_segment().first())) {
                            if (m_debug) {
                                std::cerr << "      ring.last=it->first\n";
                            }
                            ring.merge_ring(*it, m_debug);
                            m_rings.erase(it);
                            return true;
                        }
                        if (has_same_location(nr, it->last_segment().second())) {
                            if (m_debug) {
                                std::cerr << "      ring.last=it->last\n";
                            }
                            ring.merge_ring_reverse(*it, m_debug);
                            m_rings.erase(it);
                            return true;
                        }
                    }
                }
                return false;
            }

            /**
             * Check whether there are any rings that can be combined with the
             * given ring to one larger ring by prepending the other ring to
             * the start of this ring.
             * If the rings can be combined they are and the function returns
             * true.
             */
            bool possibly_combine_rings_start(ProtoRing& ring) {
                const osmium::NodeRef& nr = ring.first_segment().first();

                if (m_debug) {
                    std::cerr << "      combine_rings_start\n";
                }
                for (auto it = m_rings.begin(); it != m_rings.end(); ++it) {
                    if (&*it != &ring && !it->closed()) {
                        if (has_same_location(nr, it->last_segment().second())) {
                            if (m_debug) {
                                std::cerr << "      ring.first=it->last\n";
                            }
                            ring.swap_segments(*it);
                            ring.merge_ring(*it, m_debug);
                            m_rings.erase(it);
                            return true;
                        }
                        if (has_same_location(nr, it->first_segment().first())) {
                            if (m_debug) {
                                std::cerr << "      ring.first=it->first\n";
                            }
                            ring.reverse();
                            ring.merge_ring(*it, m_debug);
                            m_rings.erase(it);
                            return true;
                        }
                    }
                }
                return false;
            }

            bool has_closed_subring_end(ProtoRing& ring, const NodeRefSegment& segment) {
                if (ring.segments().size() < 3) {
                    return false;
                }
                if (m_debug) {
                    std::cerr << "      has_closed_subring_end()\n";
                }
                const osmium::NodeRef& nr = segment.second();
                auto end = ring.segments().end();
                for (auto it = ring.segments().begin() + 1; it != end - 1; ++it) {
                    if (has_same_location(nr, it->first())) {
                        if (m_debug) {
                            std::cerr << "        subring found at: " << *it << "\n";
                        }
                        ProtoRing new_ring(it, end);
                        ring.remove_segments(it, end);
                        if (m_debug) {
                            std::cerr << "        split into two rings:\n";
                            std::cerr << "          " << new_ring << "\n";
                            std::cerr << "          " << ring << "\n";
                        }
                        m_rings.emplace_back(new_ring);
                        return true;
                    }
                }
                return false;
            }

            bool has_closed_subring_start(ProtoRing& ring, const NodeRefSegment& segment) {
                if (ring.segments().size() < 3) {
                    return false;
                }
                if (m_debug) {
                    std::cerr << "      has_closed_subring_start()\n";
                }
                const osmium::NodeRef& nr = segment.first();
                for (auto it = ring.segments().begin() + 1; it != ring.segments().end() - 1; ++it) {
                    if (has_same_location(nr, it->second())) {
                        if (m_debug) {
                            std::cerr << "        subring found at: " << *it << "\n";
                        }
                        ProtoRing new_ring(ring.segments().begin(), it+1);
                        ring.remove_segments(ring.segments().begin(), it+1);
                        if (m_debug) {
                            std::cerr << "        split into two rings:\n";
                            std::cerr << "          " << new_ring << "\n";
                            std::cerr << "          " << ring << "\n";
                        }
                        m_rings.push_back(new_ring);
                        return true;
                    }
                }
                return false;
            }

            bool check_for_closed_subring(ProtoRing& ring) {
                if (m_debug) {
                    std::cerr << "      check_for_closed_subring()\n";
                }

                osmium::area::detail::ProtoRing::segments_type segments(ring.segments().size());
                std::copy(ring.segments().begin(), ring.segments().end(), segments.begin());
                std::sort(segments.begin(), segments.end());
                auto it = std::adjacent_find(segments.begin(), segments.end(), [this](const NodeRefSegment& s1, const NodeRefSegment& s2) {
                    return has_same_location(s1.first(), s2.first());
                });
                if (it == segments.end()) {
                    return false;
                }
                auto r1 = std::find_first_of(ring.segments().begin(), ring.segments().end(), it, it+1);
                assert(r1 != ring.segments().end());
                auto r2 = std::find_first_of(ring.segments().begin(), ring.segments().end(), it+1, it+2);
                assert(r2 != ring.segments().end());

                if (m_debug) {
                    std::cerr << "      found subring in ring " << ring << " at " << it->first() << "\n";
                }

                auto m = std::minmax(r1, r2);

                ProtoRing new_ring(m.first, m.second);
                ring.remove_segments(m.first, m.second);

                if (m_debug) {
                    std::cerr << "        split ring1=" << new_ring << "\n";
                    std::cerr << "        split ring2=" << ring << "\n";
                }

                m_rings.emplace_back(new_ring);

                return true;
            }

            void combine_rings(const NodeRefSegment& segment, ProtoRing& ring, bool at_end) {
                if (m_debug) {
                    std::cerr << " => match at " << (at_end ? "end" : "start") << " of ring\n";
                }

                if (at_end) {
                    ring.add_segment_end(segment);
                    has_closed_subring_end(ring, segment);
                    if (possibly_combine_rings_end(ring)) {
                        check_for_closed_subring(ring);
                    }
                } else {
                    ring.add_segment_start(segment);
                    has_closed_subring_start(ring, segment);
                    if (possibly_combine_rings_start(ring)) {
                        check_for_closed_subring(ring);
                    }
                }
            }

            /**
             * Append each outer ring together with its inner rings to the
             * area in the buffer.
             */
            void add_rings_to_area(osmium::osm::AreaBuilder& builder) const {
                for (const ProtoRing* ring : m_outer_rings) {
                    if (m_debug) {
                        std::cerr << "    ring " << *ring << " is outer\n";
                    }
                    osmium::osm::OuterRingBuilder ring_builder(builder.buffer(), &builder);
                    ring_builder.add_node_ref(ring->first_segment().first());
                    for (auto& segment : ring->segments()) {
                        ring_builder.add_node_ref(segment.second());
                    }
                    for (ProtoRing* inner : ring->inner_rings()) {
                        osmium::osm::InnerRingBuilder ring_builder(builder.buffer(), &builder);
                        ring_builder.add_node_ref(inner->first_segment().first());
                        for (auto& segment : inner->segments()) {
                            ring_builder.add_node_ref(segment.second());
                        }
                    }
                    builder.buffer().commit();
                }
            }

            bool add_to_existing_ring(NodeRefSegment segment) {
                int n=0;
                for (auto& ring : m_rings) {
                    if (m_debug) {
                        std::cerr << "    check against ring " << n << " " << ring;
                    }
                    if (ring.closed()) {
                        if (m_debug) {
                            std::cerr << " => ring CLOSED\n";
                        }
                    } else {
                        if (has_same_location(ring.last_segment().second(), segment.first())) {
                            combine_rings(segment, ring, true);
                            return true;
                        }
                        if (has_same_location(ring.last_segment().second(), segment.second())) {
                            segment.swap_locations();
                            combine_rings(segment, ring, true);
                            return true;
                        }
                        if (has_same_location(ring.first_segment().first(), segment.first())) {
                            segment.swap_locations();
                            combine_rings(segment, ring, false);
                            return true;
                        }
                        if (has_same_location(ring.first_segment().first(), segment.second())) {
                            combine_rings(segment, ring, false);
                            return true;
                        }
                        if (m_debug) {
                            std::cerr << " => no match\n";
                        }
                    }

                    ++n;
                }
                return false;
            }

            void check_inner_outer(ProtoRing& ring) {
                const osmium::NodeRef& min_node = ring.min_node();
                if (m_debug) {
                    std::cerr << "    check_inner_outer min_node=" << min_node << "\n";
                }

                int count = 0;
                int above = 0;

                for (auto it = m_segment_list.begin(); it != m_segment_list.end() && it->first().location().x() <= min_node.location().x(); ++it) {
                    if (!ring.contains(*it)) {
                        if (m_debug) {
                            std::cerr << "      segments for count: " << *it;
                        }
                        if (it->to_left_of(min_node.location())) {
                            ++count;
                            if (m_debug) {
                                std::cerr << " counted\n";
                            }
                        } else {
                            if (m_debug) {
                                std::cerr << " not counted\n";
                            }
                        }
                        if (it->first().location() == min_node.location()) {
                            if (it->second().location().y() > min_node.location().y()) {
                                ++above;
                            }
                        }
                        if (it->second().location() == min_node.location()) {
                            if (it->first().location().y() > min_node.location().y()) {
                                ++above;
                            }
                        }
                    }
                }

                if (m_debug) {
                    std::cerr << "      count=" << count << " above=" << above << "\n";
                }

                count += above % 2;

                if (count % 2) {
                    ring.set_inner();
                }
            }

            void check_inner_outer_roles() {
                if (m_debug) {
                    std::cerr << "    check_inner_outer_roles\n";
                }

                for (auto ringptr : m_outer_rings) {
                    for (auto segment : ringptr->segments()) {
                        if (!segment.role_outer()) {
                            ++m_inner_outer_mismatches;
                            if (m_debug) {
                                std::cerr << "      segment " << segment << " from way " << segment.way()->id() << " should have role 'outer'\n";
                            }
                            if (m_problem_reporter) {
                                m_problem_reporter->report_role_should_be_outer(m_object_id, segment.way()->id(), segment.first().location(), segment.second().location());
                            }
                        }
                    }
                }
                for (auto ringptr : m_inner_rings) {
                    for (auto segment : ringptr->segments()) {
                        if (!segment.role_inner()) {
                            ++m_inner_outer_mismatches;
                            if (m_debug) {
                                std::cerr << "      segment " << segment << " from way " << segment.way()->id() << " should have role 'inner'\n";
                            }
                            if (m_problem_reporter) {
                                m_problem_reporter->report_role_should_be_inner(m_object_id, segment.way()->id(), segment.first().location(), segment.second().location());
                            }
                        }
                    }
                }
            }

        public:

            Assembler(osmium::area::ProblemReporter* problem_reporter = nullptr) :
                m_problem_reporter(problem_reporter) {
            }

            ~Assembler() = default;

            /**
             * Enable or disable debug output to stderr. This is for Osmium
             * developers only.
             */
            void enable_debug_output(bool debug=true) {
                m_debug = debug;
                m_segment_list.enable_debug_output(debug);
            }

            void init_assembler(osmium::object_id_type id) {
                m_segment_list.clear();
                m_rings.clear();
                m_outer_rings.clear();
                m_inner_rings.clear();
                m_object_id = id;
                m_inner_outer_mismatches = 0;
            }

            /**
             * Assemble an area from the given way.
             * The resulting area is put into the out_buffer.
             */
            void operator()(const osmium::Way& way, osmium::memory::Buffer& out_buffer) {
                init_assembler(way.id());

                if (!way.ends_have_same_id()) {
                    if (m_problem_reporter) {
                        m_problem_reporter->report_duplicate_node(way.nodes().front().ref(), way.nodes().back().ref(), way.nodes().front().location());
                    }
                }

                m_segment_list.extract_segments_from_way(way, "outer");

                if (m_debug) {
                    std::cerr << "\nBuild way id()=" << way.id() << " segments.size()=" << m_segment_list.size() << "\n";
                }

                // Now create the Area object and add the attributes and tags
                // from the relation.
                osmium::osm::AreaBuilder builder(out_buffer);
                initialize_area_from_object(builder, way, 0);

                out_buffer.commit();

                if (!stage2()) {
                    return;
                }

                add_tags_to_area(builder, way);

                add_rings_to_area(builder);
            }

            /**
             * Assemble an area from the given relation and its members.
             * All members are to be found in the in_buffer at the offsets
             * given by the members parameter.
             * The resulting area is put into the out_buffer.
             */
            void operator()(const osmium::Relation& relation, const std::vector<size_t>& members, const osmium::memory::Buffer& in_buffer, osmium::memory::Buffer& out_buffer) {
                init_assembler(relation.id());

                m_segment_list.extract_segments_from_ways(relation, members, in_buffer);

                if (m_debug) {
                    std::cerr << "\nBuild relation id()=" << relation.id() << " members.size()=" << members.size() << " segments.size()=" << m_segment_list.size() << "\n";
                }

                // Now create the Area object and add the attributes and tags
                // from the relation.
                osmium::osm::AreaBuilder builder(out_buffer);
                initialize_area_from_object(builder, relation, 1);

                // From now on we have an area object without any rings in it.
                // Areas without rings are "defined" to be invalid. We commit
                // this area and the caller of the assembler will see the
                // invalid area. If all goes well, we later add the rings, commit
                // again, and thus make a valid area out of it.
                out_buffer.commit();

                if (!stage2()) {
                    return;
                }

                add_tags_to_area(builder, relation);

                add_rings_to_area(builder);

                if (m_inner_outer_mismatches == 0) {
                    auto memit = relation.members().begin();
                    for (size_t offset : members) {
                        assert(offset > 0);
                        if (!std::strcmp(memit->role(), "inner")) {
                            const osmium::Way& way = in_buffer.get<const osmium::Way>(offset);
                            if (way.is_closed() && way.tags().size() > 0) {
                                osmium::tags::KeyFilter filter(true);
                                filter.add(false, "created_by").add(false, "source").add(false, "note");
                                filter.add(false, "test:id").add(false, "test:section");

                                osmium::tags::KeyFilter::iterator fi_begin(filter, way.tags().begin(), way.tags().end());
                                osmium::tags::KeyFilter::iterator fi_end(filter, way.tags().end(), way.tags().end());

                                auto d = std::distance(fi_begin, fi_end);
                                if (d > 0) {
                                    const osmium::TagList& area_tags = builder.object().tags(); // tags of the area we just built
                                    osmium::tags::KeyFilter::iterator area_fi_begin(filter, area_tags.begin(), area_tags.end());
                                    osmium::tags::KeyFilter::iterator area_fi_end(filter, area_tags.end(), area_tags.end());

                                    if (!std::equal(fi_begin, fi_end, area_fi_begin) || d != std::distance(area_fi_begin, area_fi_end)) {
                                        operator()(way, out_buffer);
                                    }
                                }
                            }
                        }
                        ++memit;
                    }
                }
            }

            bool stage2() {
                // Now all of these segments will be sorted from bottom left
                // to top right.
                m_segment_list.sort();

                m_segment_list.erase_duplicate_segments();

                // Now we look for segments crossing each other. If there are
                // any, the multipolygon is invalid.
                // In the future this could be improved by trying to fix those
                // cases.
                if (find_intersections()) {
                    return false;
                }

                // Now iterator over all segments and add them to rings. Each segment
                // is tacked on to either end of an existing ring if possible, or a
                // new ring is started with it.
                for (const auto& segment : m_segment_list) {
                    if (m_debug) {
                        std::cerr << "  checking segment " << segment << "\n";
                    }
                    if (!add_to_existing_ring(segment)) {
                        if (m_debug) {
                            std::cerr << "    new ring for segment " << segment << "\n";
                        }
                        m_rings.emplace_back(ProtoRing(segment));
                    }
                }

                if (m_debug) {
                    std::cerr << "  Rings:\n";
                    for (auto& ring : m_rings) {
                        std::cerr << "    " << ring;
                        if (ring.closed()) {
                            std::cerr << " (closed)";
                        }
                        std::cerr << "\n";
                    }
                }

                if (check_for_open_rings()) {
                    if (m_debug) {
                        std::cerr << "  not all rings are closed\n";
                    }
                    return false;
                }

                if (m_debug) {
                    std::cerr << "  Find inner/outer...\n";
                }

                if (m_rings.size() == 1) {
                    m_outer_rings.push_back(&m_rings.front());
                } else {
                    for (auto& ring : m_rings) {
                        check_inner_outer(ring);
                        if (ring.outer()) {
                            if (!ring.is_cw()) {
                                ring.reverse();
                            }
                            m_outer_rings.push_back(&ring);
                        } else {
                            if (ring.is_cw()) {
                                ring.reverse();
                            }
                            m_inner_rings.push_back(&ring);
                        }
                    }

                    if (m_outer_rings.size() == 1) {
                        for (auto inner : m_inner_rings) {
                            m_outer_rings.front()->add_inner_ring(inner);
                        }
                    } else {
                        // sort outer rings by size, smallest first
                        std::sort(m_outer_rings.begin(), m_outer_rings.end(), [](ProtoRing* a, ProtoRing* b) {
                            return a->area() < b->area();
                        });
                        for (auto inner : m_inner_rings) {
                            for (auto outer : m_outer_rings) {
                                if (inner->is_in(outer)) {
                                    outer->add_inner_ring(inner);
                                    break;
                                }
                            }
                        }
                    }
                }

                check_inner_outer_roles();

                return true;
            }

        }; // class Assembler

    } // namespace area

} // namespace osmium

#endif // OSMIUM_AREA_ASSEMBLER_HPP
