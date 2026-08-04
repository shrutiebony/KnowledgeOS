package com.knowledgeos.service;

import com.knowledgeos.model.Document;
import com.knowledgeos.repository.DocumentRepository;
import org.springframework.stereotype.Service;

import java.util.Optional;


@Service
public class AiTextDetectionService {

    private static final String RUBRIC = """
            Estimate how likely the following text is to be AI-generated
            or AI-assisted, as opposed to written entirely by a human.
            Consider factors like uniformity of sentence structure,
            generic phrasing, and lack of idiosyncratic voice.

            Respond with ONLY raw JSON, no markdown, no explanation:
            {"score": <integer 0-100, 0=definitely human, 100=definitely AI>}
            """;

    private final StylometryService stylometryService;
    private final LlmJudgeService llmJudgeService;
    private final DocumentRepository documentRepository;

    public AiTextDetectionService(
            StylometryService stylometryService,
            LlmJudgeService llmJudgeService,
            DocumentRepository documentRepository
    ) {
        this.stylometryService = stylometryService;
        this.llmJudgeService = llmJudgeService;
        this.documentRepository = documentRepository;
    }

    public void analyze(Document document) {

        String content = document.getContent();

        if (content == null || content.isBlank()) {
            document.setContentAnalyzed(true);
            documentRepository.save(document);
            return;
        }

        StylometryService.StylometricFeatures features =
                stylometryService.computeFeatures(content);

        document.setTypeTokenRatio(features.typeTokenRatio());
        document.setAvgSentenceLength(features.avgSentenceLength());
        document.setSentenceLengthVariance(features.sentenceLengthVariance());

        Optional<Integer> score = llmJudgeService.judge(content, RUBRIC);

        score.ifPresent(s -> document.setAiTextScore(s / 100.0));

        document.setContentAnalyzed(true);
        documentRepository.save(document);
    }
}
