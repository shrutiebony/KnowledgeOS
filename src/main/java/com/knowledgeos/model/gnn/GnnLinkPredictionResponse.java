package com.knowledgeos.model.gnn;

import java.util.List;

public class GnnLinkPredictionResponse {

    private List<Prediction> predictions;

    public List<Prediction> getPredictions() { return predictions; }
    public void setPredictions(List<Prediction> predictions) { this.predictions = predictions; }

    public static class Prediction {
        private Long source;
        private Long target;
        private double score;

        public Long getSource() { return source; }
        public void setSource(Long source) { this.source = source; }

        public Long getTarget() { return target; }
        public void setTarget(Long target) { this.target = target; }

        public double getScore() { return score; }
        public void setScore(double score) { this.score = score; }
    }
}