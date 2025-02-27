# Copyright (c) 2012-2016,2019 The Linux Foundation. All rights reserved.
# Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 and
# only version 2 as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

import math

from mm import pfn_to_page
from parser_util import register_parser, RamParser
from print_out import print_out_str

# /kernel/msm-4.4/mm/slub.c
OO_SHIFT = 16
PAGE_SHIFT = 12

@register_parser('--slabsummary_partial', 'print summary of partial slab', optional=True)
class Slabinfo_summary(RamParser):

    def cal_free_pages(
                        self, ramdump,
                        start,  slab_lru_offset,
                        max_page):
        page = self.ramdump.read_word(start)
        totalfree = 0
        total_objects = 0
        seen = []
        mapcount = 0
        objects = 0
        inuse = 0

        if page == 0:
            return totalfree, total_objects

        while page != start:
            if page is None or page == 0:
                return totalfree, total_objects
            if page in seen:
                return totalfree, total_objects
            if page > max_page:
                return totalfree, total_objects
            seen.append(page)
            page = page - slab_lru_offset
            if (self.ramdump.kernel_version <= (4, 14)):
                mapcount = self.ramdump.read_structure_field(
                                                        page, 'struct page', '_mapcount')
            else:
                mapcount = self.ramdump.read_structure_field(
                                                        page, 'struct page', 'counters')
            if mapcount == None:
                return totalfree, total_objects
            inuse = mapcount & 0x0000FFFF
            objects = (mapcount >> 16) & 0x00007FFF

            if inuse < objects:
                freeobj = objects - inuse
                total_objects = total_objects + objects
                totalfree = totalfree + freeobj

            page = self.ramdump.read_word(page + slab_lru_offset)
            if (totalfree < 0) or (total_objects < 0) or (totalfree > total_objects):
                return 0, 0

        return totalfree, total_objects

    # Currently with assumption there is only one numa node
    def print_slab_summary(self, slab_out):
        total_freeobjects = 0
        original_slab = self.ramdump.addr_lookup('slab_caches')
        cpus = self.ramdump.get_num_cpus()
        slab_list_offset = self.ramdump.field_offset(
            'struct kmem_cache', 'list')
        slab_name_offset = self.ramdump.field_offset(
            'struct kmem_cache', 'name')
        slab_node_offset = self.ramdump.field_offset(
            'struct kmem_cache', 'node')
        if (self.ramdump.kernel_version >= (6, 1, 0)):
            cpu_cache_page_offset = self.ramdump.field_offset(
                'struct kmem_cache_cpu', 'slab')
        else:
            cpu_cache_page_offset = self.ramdump.field_offset(
                'struct kmem_cache_cpu', 'page')
        cpu_slab_offset = self.ramdump.field_offset(
            'struct kmem_cache', 'cpu_slab')
        slab_partial_offset = self.ramdump.field_offset(
            'struct kmem_cache_node', 'partial')
        slab = self.ramdump.read_word(original_slab)
        slab_lru_offset = self.ramdump.field_offset(
                                         'struct page', 'lru')
        max_pfn_addr = self.ramdump.addr_lookup('max_pfn')
        max_pfn = self.ramdump.read_word(max_pfn_addr)
        max_page = pfn_to_page(self.ramdump, max_pfn)
        format_string = '\n{0:35} {1:9} {2:10} {3:10} {4:8}K {5:10} {6:10}K'
        slab_out.write("===================================================================================================\n")
        slab_out.write("==================================== Summary of partial slabs =====================================\n")
        slab_out.write("===================================================================================================\n")
        slab_out.write(
            '{0:35} {1:9} {2:10} {3:10} {4:8} {5:10} {6:10}'.format(
                                        "NAME", "OBJSIZE", "ALLOCATED",
                                        "TOTAL", "TOTAL*SIZE", "SLABS",
                                        "SSIZE"))

        while slab != original_slab:
            total_freeobjects = 0
            total_objects = 0
            freeobjs = 0
            objects = 0
            slab = slab - slab_list_offset
            slab_name_addr = self.ramdump.read_word(
                                        slab + slab_name_offset)
            # actually an array but again, no numa
            slab_node_addr = self.ramdump.read_word(
                                        slab + slab_node_offset)
            slab_name = self.ramdump.read_cstring(
                                        slab_name_addr, 48)
            cpu_slab_addr = self.ramdump.read_word(
                                        slab + cpu_slab_offset)
            oo = self.ramdump.read_structure_field(
                        slab, 'struct kmem_cache', 'oo')
            obj_size = self.ramdump.read_structure_field(
                        slab, 'struct kmem_cache', 'object_size')
            objsize_w_metadata = self.ramdump.read_structure_field(
                        slab, 'struct kmem_cache', 'size')
            nr_total_objects = self.ramdump.read_structure_field(
                        slab_node_addr,
                        'struct kmem_cache_node', 'total_objects')
            num_slabs = self.ramdump.read_structure_field(
                        slab_node_addr,
                        'struct kmem_cache_node', 'nr_slabs')
            nr_partial = self.ramdump.read_structure_field(slab_node_addr,
                         'struct kmem_cache_node', 'nr_partial')

            if nr_partial > 0:
                # per cpu slab
                for i in range(0, cpus):
                    cpu_slabn_addr = self.ramdump.read_word(
                                                cpu_slab_addr, cpu=i)
                    if cpu_slabn_addr == 0 or None:
                        break
                    freeobjs, objects = self.cal_free_pages(self.ramdump,
                                    (cpu_slabn_addr + cpu_cache_page_offset),
                                    slab_lru_offset, max_page)
                    if freeobjs == None or objects == None:
                        continue
                    total_freeobjects = total_freeobjects + freeobjs
                    total_objects = total_objects + objects

                freeobjs, objects = self.cal_free_pages(self.ramdump,
                                    slab_node_addr + slab_partial_offset,
                                    slab_lru_offset, max_page)
                total_freeobjects = total_freeobjects + freeobjs
                total_objects = total_objects + objects

                nr_total_objects = total_objects
                total_allocated = nr_total_objects - total_freeobjects
                page_order = oo >> OO_SHIFT
                slab_size = int(math.pow(2, page_order + PAGE_SHIFT))
                slab_size = slab_size / 1024
                slab_out.write(format_string.format(
                                    slab_name, obj_size, total_allocated,
                                    nr_total_objects,
                                    (objsize_w_metadata * nr_total_objects)/1024,
                                    num_slabs, slab_size))
            slab = self.ramdump.read_word(slab + slab_list_offset)

    def parse(self):
        print_out_str("Summary of partial slabs will be provided")
        slab_out = self.ramdump.open_file('slabsummary_partial.txt')
        self.print_slab_summary(slab_out)
        slab_out.close()
