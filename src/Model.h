#pragma once
/*
 * Model.h — Assimp tabanlı 3D model yükleyici
 *
 * OBJ + MTL formatını destekler:
 *   - Her mesh için VAO/VBO/EBO oluşturur
 *   - MTL'den Kd (diffuse) rengini okur
 *   - Eğer varsa colormap.png tekstürünü yükler (stb_image kullanır)
 */

#include <GL/glew.h>
#include <glm/glm.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <string>
#include <vector>
#include <iostream>

// stb_image bildirimleri (implementation stb_impl.cpp'de)
#include "stb_image.h"

// ─── Tek vertex: pozisyon + normal + UV ───────────────────────
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

// ─── GPU'ya yüklenmiş tek mesh ─────────────────────────────────
struct Mesh {
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLsizei indexCount = 0;
    glm::vec3 color{0.8f, 0.8f, 0.8f};   // MTL Kd rengi
    GLuint texID = 0;                      // 0 = tekstür yok
    bool hasTexture = false;

    void draw() const {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    void release() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &ebo);
        if (texID) glDeleteTextures(1, &texID);
    }
};

// ─── Bir OBJ dosyasını temsil eden model ──────────────────────
class Model {
public:
    std::vector<Mesh> meshes;
    bool loaded = false;

    bool load(const std::string& path) {
        Assimp::Importer imp;
        const aiScene* scene = imp.ReadFile(path,
            aiProcess_Triangulate       |   // Polygon → üçgen
            aiProcess_GenSmoothNormals  |   // Normal vektörler hesapla
            aiProcess_FlipUVs           |   // OpenGL için UV'yi çevir
            aiProcess_JoinIdenticalVertices);

        if (!scene || !scene->mRootNode ||
            scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
        {
            std::cerr << "[Model] Yüklenemedi: " << path
                      << "\n  " << imp.GetErrorString() << "\n";
            return false;
        }

        // Modelin dizini: texture yolları için gerekli
        dir_ = path.substr(0, path.find_last_of("/\\"));

        processNode(scene->mRootNode, scene);
        loaded = true;
        std::cout << "[Model] Yüklendi: " << path
                  << " (" << meshes.size() << " mesh)\n";
        return true;
    }

    void draw() const {
        for (auto& m : meshes) m.draw();
    }

    void release() {
        for (auto& m : meshes) m.release();
        meshes.clear();
    }

private:
    std::string dir_;

    void processNode(aiNode* node, const aiScene* scene) {
        for (unsigned i = 0; i < node->mNumMeshes; ++i)
            meshes.push_back(buildMesh(scene->mMeshes[node->mMeshes[i]], scene));
        for (unsigned i = 0; i < node->mNumChildren; ++i)
            processNode(node->mChildren[i], scene);
    }

    Mesh buildMesh(aiMesh* m, const aiScene* scene) {
        std::vector<Vertex>      verts;
        std::vector<unsigned int> idx;

        // Vertex verisini topla
        for (unsigned i = 0; i < m->mNumVertices; ++i) {
            Vertex v;
            v.pos    = {m->mVertices[i].x, m->mVertices[i].y, m->mVertices[i].z};
            v.normal = m->HasNormals()
                ? glm::vec3(m->mNormals[i].x, m->mNormals[i].y, m->mNormals[i].z)
                : glm::vec3(0, 1, 0);
            v.uv = (m->mTextureCoords[0])
                ? glm::vec2(m->mTextureCoords[0][i].x, m->mTextureCoords[0][i].y)
                : glm::vec2(0, 0);
            verts.push_back(v);
        }

        // Index verisini topla
        for (unsigned i = 0; i < m->mNumFaces; ++i)
            for (unsigned j = 0; j < m->mFaces[i].mNumIndices; ++j)
                idx.push_back(m->mFaces[i].mIndices[j]);

        // VAO/VBO/EBO oluştur
        Mesh mesh;
        mesh.indexCount = static_cast<GLsizei>(idx.size());
        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vbo);
        glGenBuffers(1, &mesh.ebo);

        glBindVertexArray(mesh.vao);

        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,
            verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
            idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);

        // layout(location=0) aPos, =1 aNormal, =2 aUV
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
            (void*)offsetof(Vertex, pos));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
            (void*)offsetof(Vertex, normal));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
            (void*)offsetof(Vertex, uv));
        glEnableVertexAttribArray(2);

        glBindVertexArray(0);

        // Materyal rengini ve (varsa) tekstürü yükle
        if (m->mMaterialIndex < scene->mNumMaterials) {
            aiMaterial* mat = scene->mMaterials[m->mMaterialIndex];
            aiColor3D kd(0.8f, 0.8f, 0.8f);
            mat->Get(AI_MATKEY_COLOR_DIFFUSE, kd);
            mesh.color = {kd.r, kd.g, kd.b};

            // Diffuse texture var mı?
            if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
                aiString texPath;
                mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath);
                std::string fullPath = dir_ + "/" + texPath.C_Str();
                mesh.texID     = loadTexture(fullPath);
                mesh.hasTexture = (mesh.texID != 0);
            }
        }

        return mesh;
    }

    static GLuint loadTexture(const std::string& path) {
        int w, h, ch;
        stbi_set_flip_vertically_on_load(false);
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 0);
        if (!data) {
            std::cerr << "[Texture] Bulunamadı: " << path << "\n";
            return 0;
        }
        GLenum fmt = (ch == 4) ? GL_RGBA : GL_RGB;
        GLuint texID;
        glGenTextures(1, &texID);
        glBindTexture(GL_TEXTURE_2D, texID);
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        stbi_image_free(data);
        std::cout << "[Texture] Yüklendi: " << path << " (" << w << "x" << h << ")\n";
        return texID;
    }
};
