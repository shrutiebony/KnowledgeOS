package com.knowledgeos.service;

import org.springframework.stereotype.Service;

import java.net.URI;

@Service
public class UrlNormalizer {


    public String normalize(String url) {

        try {

            URI uri = new URI(url);


            String scheme = uri.getScheme();

            String host = uri.getHost();

            String path = uri.getPath();


            if (scheme == null || host == null) {
                return url;
            }
            scheme = scheme.toLowerCase();
            host = host.toLowerCase();
            if (path == null || path.isEmpty()) {
                path = "/";
            }
            if (path.endsWith("/") && path.length() > 1) {
                path = path.substring(0, path.length() - 1);
            }
            return scheme + "://" + host + path;
        } catch (Exception e) {

            return url;
        }
    }
}