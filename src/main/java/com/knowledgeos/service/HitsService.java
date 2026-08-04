package com.knowledgeos.service;

import org.springframework.stereotype.Service;

import java.util.*;

@Service
public class HitsService {


    public Map<Long, double[]> calculateHits(
            Map<Long, List<Long>> graph,
            int iterations
    ) {

        Map<Long, Double> authority = new HashMap<>();
        Map<Long, Double> hub = new HashMap<>();


        // Initialize scores
        for (Long node : graph.keySet()) {
            authority.put(node, 1.0);
            hub.put(node, 1.0);
        }


        // HITS iterations
        for (int i = 0; i < iterations; i++) {

            Map<Long, Double> newAuthority = new HashMap<>();
            Map<Long, Double> newHub = new HashMap<>();


            // Authority = sum of incoming hub scores
            for (Long node : graph.keySet()) {

                double score = 0;

                for (Long other : graph.keySet()) {

                    if (graph.get(other).contains(node)) {
                        score += hub.get(other);
                    }
                }

                newAuthority.put(node, score);
            }



            // Hub = sum of outgoing authority scores
            for (Long node : graph.keySet()) {

                double score = 0;

                for (Long target : graph.get(node)) {
                    score += newAuthority.get(target);
                }

                newHub.put(node, score);
            }



            // Normalize authority vector
            double authorityNorm = Math.sqrt(
                    newAuthority.values()
                            .stream()
                            .mapToDouble(x -> x * x)
                            .sum()
            );


            if (authorityNorm != 0) {

                for (Long node : newAuthority.keySet()) {

                    newAuthority.put(
                            node,
                            newAuthority.get(node) / authorityNorm
                    );
                }
            }



            // Normalize hub vector
            double hubNorm = Math.sqrt(
                    newHub.values()
                            .stream()
                            .mapToDouble(x -> x * x)
                            .sum()
            );


            if (hubNorm != 0) {

                for (Long node : newHub.keySet()) {

                    newHub.put(
                            node,
                            newHub.get(node) / hubNorm
                    );
                }
            }



            authority = newAuthority;
            hub = newHub;
        }



        // Final result
        Map<Long, double[]> result = new HashMap<>();

        for (Long node : graph.keySet()) {

            result.put(
                    node,
                    new double[]{
                            authority.get(node),
                            hub.get(node)
                    }
            );
        }


        return result;
    }
}