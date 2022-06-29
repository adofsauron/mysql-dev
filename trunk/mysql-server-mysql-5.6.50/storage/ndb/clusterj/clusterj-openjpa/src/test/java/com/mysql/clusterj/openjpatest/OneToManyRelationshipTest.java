/*
   Copyright 2010 Sun Microsystems, Inc.
   All rights reserved. Use is subject to license terms.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

package com.mysql.clusterj.openjpatest;

import com.mysql.clusterj.jpatest.AbstractJPABaseTest;
import com.mysql.clusterj.jpatest.model.A;
import com.mysql.clusterj.jpatest.model.B0;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;

/**
 *
 */
public class OneToManyRelationshipTest extends AbstractJPABaseTest {

    private int NUMBER_OF_A = 2;
    private int OFFSET_A = 100;
    private int NUMBER_OF_B = 4;
    private int OFFSET_B = 10;
    private A a0;
    // set this to true for debug output
    private boolean print = false;
    private List<A> as = new ArrayList<A>();

    @Override
    public void setUp() {
        super.setUp();
    }

    public void test() {
        em = emf.createEntityManager();
        print("Removing " + NUMBER_OF_A + " instances of A.");
        begin();
        for (int i = OFFSET_A; i < OFFSET_A + NUMBER_OF_A; ++i) {
            A a = em.find(A.class, i);
            em.remove(a);
        }
        print("Removing " + NUMBER_OF_B + " instances of B.");
        for (int i = OFFSET_B; i < OFFSET_B + NUMBER_OF_B; ++i) {
            B0 b = em.find(B0.class, i);
            em.remove(b);
        }
        commit();
        em.close();

        em = emf.createEntityManager();
        begin();
        Collection<B0> bs = new HashSet<B0>();
        print("Creating " + NUMBER_OF_A + " instances of A.");
        for (int i = OFFSET_A; i < OFFSET_A + NUMBER_OF_A; ++i) {
            a0 = A.create(i);
            em.persist(a0);
        }
        print("Creating " + NUMBER_OF_B + " instances of B.");
        for (int i = OFFSET_B; i < OFFSET_B + NUMBER_OF_B; ++i) {
            B0 b = B0.create(i);
            b.setA(a0);
            bs.add(b);
            em.persist(b);
        }
        a0.setB0s(bs);
        print("Before commit, " + a0.toString());
        for (B0 b:bs){print(b.toString());}
        commit();
        em.close();

        em = emf.createEntityManager();
        print("Finding " + NUMBER_OF_A + " instances of A.");
        begin();
        for (int i = OFFSET_A; i < OFFSET_A + NUMBER_OF_A; ++i) {
            a0 = em.find(A.class, i);
            print(a0.toString());
        }
        print("Finding " + NUMBER_OF_B + " instances of B.");
        for (int i = OFFSET_B; i < OFFSET_B + NUMBER_OF_B; ++i) {
            B0 b = em.find(B0.class, i);
            print(b.toString());
        }
        commit();
        em.close();

        em = emf.createEntityManager();
        print("Finding 1 instance of A.");
        begin();
        A a = em.find(A.class, OFFSET_A);
        print("Finding 2 instances of B.");
        for (int i = OFFSET_B; i < OFFSET_B + NUMBER_OF_B; i += 2) {
            B0 b = em.find(B0.class, i);
            // update every other one
            b.setA(a);
            print(b.toString());
        }
        print("After update: " + a0.toString());
        commit();
        em.close();

        em = emf.createEntityManager();
        print("Finding " + NUMBER_OF_A + " instances of A.");
        begin();
        for (int i = OFFSET_A; i < OFFSET_A + NUMBER_OF_A; ++i) {
            a = em.find(A.class, i);
            as.add(a);
            print(a.toString());
        }
        print("Finding " + NUMBER_OF_B + " instances of B.");
        for (int i = OFFSET_B; i < OFFSET_B + NUMBER_OF_B; ++i) {
            B0 b = em.find(B0.class, i);
            print(b.toString());
            if (0 == i%2) {
                assertEquals("Mismatch in relationship a", as.get(0), b.getA());
                assertTrue("A.b0s should contain b", as.get(0).getB0s().contains(b));
            } else {
                assertEquals("Mismatch in relationship a", as.get(1), b.getA());
                assertTrue("A.b0s should contain b", as.get(1).getB0s().contains(b));
            }
        }
        commit();
        em.close();

    }

    private void print(String string) {
        if (print) {
            System.out.println(string);
        }
    }

}
