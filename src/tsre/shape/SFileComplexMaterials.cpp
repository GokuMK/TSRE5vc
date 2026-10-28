/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// Material editing of complete MSTS shapes (task shapes 13, the Shape
// Viewer's materials editor): the primitive states, their shaders, textures,
// light materials and PBR factors (.sd), and which primitives draw with them.
#include "SFileComplexData.h"
#include <QFileInfo>
#include <QStringList>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <tsre/fileFunctions/ContentPath.h>

using SFileDetail::Node;

namespace {
Node child(Node n, const char *name) { return n.child(name); }
// The blocks of a list (prim_states, images...): its children so named.
std::vector<Node> list(Node parent, const char *name) {
    std::vector<Node> out;
    for (auto n : parent.children(name))
        out.push_back(n);
    return out;
}
bool setCount(Node parent, const char *name) {
    int count = 0;
    for (auto n : parent.children(name)) {
        (void)n;
        ++count;
    }
    return parent.setScalar(0, QString::number(count));
}
// A primitives block's drawn primitives (the parts, in order).
bool drawn(const Node &n) {
    const QString name = n.name();
    return name == "indexed_trilist" || name == "indexed_line_list" || name == "point_list";
}
QString number(double value) {
    return QString::number(value, 'g', 6);
}
// A .sd (text, UTF-16 with its byte order mark) of these lines inside its
// shape block, read into a document.
bool metadataDocument(const QString &body, SFileDetail::Document &out) {
    const QString text = QStringLiteral("SIMISA@@@@@@@@@@JINX0t1t______\r\n\r\nshape ( ") + body + QStringLiteral("\r\n)\r\n");
    QByteArray bytes("\xff\xfe", 2);
    for (QChar c : text) {
        char pair[2];
        qToLittleEndian(c.unicode(), reinterpret_cast<uchar *>(pair));
        bytes.append(pair, 2);
    }
    return out.readBytes(bytes) && out.root;
}
QString quoted(const QString &s) {
    return s.contains(' ') || s.isEmpty() ? '"' + s + '"' : s;
}
} // namespace

struct SFileComplex::Snapshot {
    std::unique_ptr<SFileDetail::Document> document, metadata;
    bool metadataEdited = false;
};

bool SFileComplex::editable(QString *error) const {
    if (d->retention != Retention::Complete || !d->document || d->health == Health::Broken) {
        if (error)
            //% "Materials can be edited only in a shape loaded completely."
            *error = qtTrId("shape.materials.error.not.editable");
        return false;
    }
    return true;
}

void SFileComplex::afterMaterialEdit() {
    d->edited = true;
    releaseGL();
    extract();
}

int SFileComplex::materialCount() const {
    return int(d->materials.size());
}

bool SFileComplex::materialInfo(int material, MaterialInfo &out) const {
    if (!d->document || material < 0 || material >= int(d->materials.size()))
        return false;
    const Node root = d->document->root;
    const std::vector<Node> states = list(child(root, "prim_states"), "prim_state");
    if (material >= int(states.size()))
        return false;
    const Node state = states[material];
    const std::vector<Node> shaders = list(child(root, "shader_names"), "named_shader");
    const std::vector<Node> textures = list(child(root, "textures"), "texture");
    const std::vector<Node> images = list(child(root, "images"), "image");
    const std::vector<Node> vertexStates = list(child(root, "vtx_states"), "vtx_state");
    out = MaterialInfo();
    out.name = state.label();
    const int shader = state.integer(1, -1);
    out.shader = shader >= 0 && shader < int(shaders.size()) ? shaders[shader].scalar(0) : QString();
    const Node tex = child(state, "tex_idxs");
    for (int i = 1; i < tex.scalarCount(); ++i) {
        const int t = tex.integer(i, -1);
        const int image = t >= 0 && t < int(textures.size()) ? textures[t].integer(0, -1) : -1;
        out.textures << (image >= 0 && image < int(images.size()) ? images[image].scalar(0) : QString());
    }
    out.zBias = float(state.number(2));
    out.alphaTest = state.integer(4) == 1;
    const int vertexState = state.integer(3, -1);
    if (vertexState >= 0 && vertexState < int(vertexStates.size())) {
        out.matrix = vertexStates[vertexState].integer(1);
        out.lightMaterial = vertexStates[vertexState].integer(2);
    }
    if (out.matrix >= 0 && out.matrix < int(d->matrices.size()))
        out.matrixName = d->matrices[out.matrix].name;
    for (const auto &lod : d->lods)
        for (const auto &mesh : lod.meshes)
            for (const auto &part : mesh.parts)
                if (part.material == material)
                    ++out.uses;
    out.factors = d->materials[material].factors;
    return true;
}

QVector<int> SFileComplex::primitivesWithMaterial(int level, int material) const {
    QVector<int> out;
    if (level < 0 || level >= int(d->lods.size()))
        return out;
    int number = 0;
    for (const auto &mesh : d->lods[level].meshes)
        for (const auto &part : mesh.parts) {
            if (part.material == material)
                out << number;
            ++number;
        }
    return out;
}

int SFileComplex::drawnLevel(unsigned int stateId) const {
    return stateId < d->states.size() ? d->states[stateId].lod : 0;
}

// The shader's index in shader_names, added when missing.
static int shaderIndex(Node root, const QString &name, QString *error) {
    Node shaders = child(root, "shader_names");
    const std::vector<Node> named = list(shaders, "named_shader");
    for (size_t i = 0; i < named.size(); ++i)
        if (named[i].scalar(0).compare(name, Qt::CaseInsensitive) == 0)
            return int(i);
    if (named.empty()) {
        if (error)
            //% "The shape has no shader names."
            *error = qtTrId("shape.materials.error.no.shaders");
        return -1;
    }
    Node added = shaders.insertCopy(named.front());
    if (!added || !added.setScalar(0, name, error) || !setCount(shaders, "named_shader"))
        return -1;
    return int(named.size());
}

// The texture entry of an image file (by name), the image and the texture
// added when missing, the new texture copying the first one's filtering.
static int textureIndex(Node root, const QString &file, QString *error) {
    Node images = child(root, "images");
    Node textures = child(root, "textures");
    const std::vector<Node> imageList = list(images, "image");
    const std::vector<Node> textureList = list(textures, "texture");
    if (imageList.empty() || textureList.empty()) {
        if (error)
            //% "The shape has no textures to copy."
            *error = qtTrId("shape.materials.error.no.textures");
        return -1;
    }
    int image = -1;
    for (size_t i = 0; i < imageList.size() && image < 0; ++i)
        if (imageList[i].scalar(0).compare(file, Qt::CaseInsensitive) == 0)
            image = int(i);
    if (image < 0) {
        Node added = images.insertCopy(imageList.front());
        if (!added || !added.setScalar(0, file, error) || !setCount(images, "image"))
            return -1;
        image = int(imageList.size());
    }
    for (size_t i = 0; i < textureList.size(); ++i)
        if (textureList[i].integer(0, -1) == image)
            return int(i);
    Node added = textures.insertCopy(textureList.front());
    if (!added || !added.setScalar(0, QString::number(image), error) || !setCount(textures, "texture"))
        return -1;
    return int(textureList.size());
}

// A vertex state as `source` with another matrix or light material: an
// equal one, else a copy added.
static int vertexStateLike(Node root, int source, int matrix, int light, QString *error) {
    Node vertexStates = child(root, "vtx_states");
    const std::vector<Node> states = list(vertexStates, "vtx_state");
    if (source < 0 || source >= int(states.size()))
        return -1;
    const Node from = states[source];
    for (size_t i = 0; i < states.size(); ++i) {
        const Node s = states[i];
        bool same = s.scalarCount() == from.scalarCount() && s.integer(1) == matrix && s.integer(2) == light;
        for (int k = 0; same && k < s.scalarCount(); ++k)
            same = k == 1 || k == 2 || s.scalar(k) == from.scalar(k);
        if (same)
            return int(i);
    }
    Node added = vertexStates.insertCopy(from);
    if (!added || !added.setScalar(1, QString::number(matrix), error)
            || !added.setScalar(2, QString::number(light), error) || !setCount(vertexStates, "vtx_state"))
        return -1;
    return int(states.size());
}

// The primitive states using a vertex state.
static int vertexStateUsers(Node root, int vertexState) {
    int users = 0;
    for (auto s : child(root, "prim_states").children("prim_state"))
        if (s->integer(3, -1) == vertexState)
            ++users;
    return users;
}

bool SFileComplex::setMaterial(int material, const MaterialInfo &info, QString *error) {
    if (!editable(error))
        return false;
    Node root = d->document->root;
    const std::vector<Node> states = list(child(root, "prim_states"), "prim_state");
    if (material < 0 || material >= int(states.size()))
        return false;
    Node state = states[material];
    state.setLabel(info.name);
    // Shader.
    const int shader = shaderIndex(root, info.shader.trimmed().isEmpty() ? QStringLiteral("TexDiff") : info.shader.trimmed(),
                                   error);
    if (shader < 0 || !state.setScalar(1, QString::number(shader), error))
        return false;
    // Textures (trailing empty slots left out).
    QStringList files = info.textures;
    while (!files.isEmpty() && files.last().trimmed().isEmpty())
        files.removeLast();
    QStringList tex{QString::number(files.size())};
    for (const QString &file : files) {
        const int t = file.trimmed().isEmpty() ? -1 : textureIndex(root, file.trimmed(), error);
        if (t < 0 && !file.trimmed().isEmpty())
            return false;
        tex << QString::number(t);
    }
    Node texNode = child(state, "tex_idxs");
    if (!texNode || !texNode.setScalars(tex, error))
        return false;
    // Light material, on a vertex state of its own when others share it.
    const std::vector<Node> vertexStates = list(child(root, "vtx_states"), "vtx_state");
    const int vertexState = state.integer(3, -1);
    if (vertexState < 0 || vertexState >= int(vertexStates.size()))
        return false;
    if (vertexStates[vertexState].integer(2) != info.lightMaterial) {
        if (vertexStateUsers(root, vertexState) == 1) {
            Node v = vertexStates[vertexState];
            if (!v.setScalar(2, QString::number(info.lightMaterial), error))
                return false;
        } else {
            const int own = vertexStateLike(root, vertexState, vertexStates[vertexState].integer(1), info.lightMaterial,
                                            error);
            if (own < 0 || !state.setScalar(3, QString::number(own), error))
                return false;
        }
    }
    if (!state.setScalar(2, number(info.zBias), error) || !state.setScalar(4, info.alphaTest ? "1" : "0", error))
        return false;
    // PBR factors in the .sd, by the base colour texture.
    if (MstsMaterials::pbr(MstsMaterials::shader(info.shader)) && !files.isEmpty()
            && !setPbrFactors(MstsMaterials::key(files.first()), info.factors, error))
        return false;
    afterMaterialEdit();
    return true;
}

int SFileComplex::addMaterial(int copyOf, QString *error) {
    if (!editable(error))
        return -1;
    Node states = child(d->document->root, "prim_states");
    const std::vector<Node> list_ = list(states, "prim_state");
    if (copyOf < 0 || copyOf >= int(list_.size()))
        return -1;
    Node added = states.insertCopy(list_[copyOf]);
    if (!added || !setCount(states, "prim_state"))
        return -1;
    afterMaterialEdit();
    return int(list_.size());
}

bool SFileComplex::removeMaterial(int material, QString *error) {
    if (!editable(error))
        return false;
    Node root = d->document->root;
    Node states = child(root, "prim_states");
    const std::vector<Node> list_ = list(states, "prim_state");
    if (material < 0 || material >= int(list_.size()))
        return false;
    MaterialInfo info;
    if (materialInfo(material, info) && info.uses > 0) {
        if (error)
            //% "The material is drawn by %1 primitives: give them another one first."
            *error = qtTrId("shape.materials.error.in.use").arg(info.uses);
        return false;
    }
    if (!states.removeChild(list_[material]) || !setCount(states, "prim_state"))
        return false;
    // The states after it move down one.
    for (auto control : child(root, "lod_controls").children("lod_control"))
        for (auto level : child(*control, "distance_levels").children("distance_level"))
            for (auto object : child(*level, "sub_objects").children("sub_object"))
                for (auto p : child(*object, "primitives").children("prim_state_idx")) {
                    const int index = p->integer(0, -1);
                    if (index > material)
                        p->setScalar(0, QString::number(index - 1));
                }
    afterMaterialEdit();
    return true;
}

bool SFileComplex::assignMaterial(int level, const QVector<int> &primitives, int material, bool allLevels,
                                  QString *error) {
    if (!editable(error))
        return false;
    if (level < 0 || level >= int(d->lods.size()) || material < 0 || material >= int(d->materials.size()))
        return false;
    Node root = d->document->root;
    // The primitives to change: (level, mesh, part); with allLevels, the
    // other levels' primitives of the same matrix and material.
    struct Target { int level, mesh, part; };
    std::vector<Target> targets;
    QSet<QPair<int, int>> picked; // (matrix, material) of the picked ones
    int number = 0;
    const auto &lod = d->lods[level];
    for (int m = 0; m < int(lod.meshes.size()); ++m)
        for (int k = 0; k < int(lod.meshes[m].parts.size()); ++k, ++number)
            if (primitives.contains(number)) {
                targets.push_back({level, m, k});
                const int state = lod.meshes[m].parts[k].material;
                picked.insert({d->materials[state].matrix, state});
            }
    if (allLevels)
        for (int l = 0; l < int(d->lods.size()); ++l) {
            if (l == level)
                continue;
            for (int m = 0; m < int(d->lods[l].meshes.size()); ++m)
                for (int k = 0; k < int(d->lods[l].meshes[m].parts.size()); ++k) {
                    const int state = d->lods[l].meshes[m].parts[k].material;
                    if (picked.contains({d->materials[state].matrix, state}))
                        targets.push_back({l, m, k});
                }
        }
    if (targets.empty())
        return true;
    // A state like the material for each matrix: its own where the matrix
    // agrees, else an equal state of that matrix, else a copy.
    const std::vector<Node> stateNodes = list(child(root, "prim_states"), "prim_state");
    QHash<int, int> stateForMatrix;
    auto stateFor = [&](int matrix) -> int {
        if (stateForMatrix.contains(matrix))
            return stateForMatrix.value(matrix);
        int result = -1;
        if (d->materials[material].matrix == matrix) {
            result = material;
        } else {
            const Node m = stateNodes[material];
            const int vertexState = vertexStateLike(root, m.integer(3, -1), matrix,
                                                    d->materials[material].light, error);
            if (vertexState < 0)
                return -1;
            const std::vector<Node> states = list(child(root, "prim_states"), "prim_state");
            for (size_t i = 0; i < states.size() && result < 0; ++i) {
                const Node s = states[i];
                bool same = s.scalarCount() == m.scalarCount() && s.integer(3, -1) == vertexState;
                for (int k = 0; same && k < s.scalarCount(); ++k)
                    same = k == 3 || s.scalar(k) == m.scalar(k);
                const Node a = child(s, "tex_idxs"), b = child(m, "tex_idxs");
                same = same && a.scalarCount() == b.scalarCount();
                for (int k = 0; same && k < a.scalarCount(); ++k)
                    same = a.scalar(k) == b.scalar(k);
                if (same)
                    result = int(i);
            }
            if (result < 0) {
                Node statesNode = child(root, "prim_states");
                Node added = statesNode.insertCopy(m);
                if (!added || !added.setScalar(3, QString::number(vertexState), error)
                        || !setCount(statesNode, "prim_state"))
                    return -1;
                result = int(states.size());
            }
        }
        stateForMatrix.insert(matrix, result);
        return result;
    };
    // The document's primitives block of each target's sub-object.
    for (const Target &t : targets) {
        const auto &mesh = d->lods[t.level].meshes[t.mesh];
        const int current = mesh.parts[t.part].material;
        const int state = stateFor(d->materials[current].matrix);
        if (state < 0)
            return false;
        if (state == current)
            continue;
        const auto &lodData = d->lods[t.level];
        Node control = child(root, "lod_controls").children("lod_control")[size_t(lodData.control)];
        Node levelNode = child(control, "distance_levels").children("distance_level")[size_t(lodData.sourceLevel)];
        Node object = child(levelNode, "sub_objects").children("sub_object")[size_t(mesh.subobject)];
        Node primitivesNode = child(object, "primitives");
        if (!primitivesNode)
            return false;
        // The part's block, and the state in effect before and after it.
        std::vector<Node> children;
        for (auto c : primitivesNode.children())
            children.push_back(c);
        int drawnIndex = -1, at = -1;
        for (int i = 0; i < int(children.size()); ++i)
            if (drawn(children[i]) && ++drawnIndex == t.part) {
                at = i;
                break;
            }
        if (at < 0)
            return false;
        SFileDetail::Document snippet;
        if (!metadataDocument(QStringLiteral("x prim_state_idx ( %1 ) prim_state_idx ( %2 )").arg(state).arg(current),
                              snippet))
            return false;
        const std::vector<Node> stateIdx = list(snippet.root, "prim_state_idx");
        if (stateIdx.size() != 2)
            return false;
        primitivesNode.insertCopy(stateIdx[0], children[at]);
        // The next drawn primitive keeps the state it had.
        if (at + 1 < int(children.size()) && drawn(children[at + 1]))
            primitivesNode.insertCopy(stateIdx[1], children[at + 1]);
        // States that set nothing: one followed by another or by the end,
        // or setting the state already in effect.
        std::vector<Node> now;
        for (auto c : primitivesNode.children())
            now.push_back(c);
        int effective = -1;
        for (int i = 0; i < int(now.size()); ++i) {
            if (now[i].name() != "prim_state_idx")
                continue;
            const bool lastOrState = i + 1 == int(now.size()) || now[i + 1].name() == "prim_state_idx";
            const int value = now[i].integer(0, -1);
            if (lastOrState || value == effective) {
                primitivesNode.removeChild(now[i]);
                continue;
            }
            effective = value;
        }
        int count = 0;
        for (auto c : primitivesNode.children()) {
            (void)c;
            ++count;
        }
        if (!primitivesNode.setScalar(0, QString::number(count), error))
            return false;
        // MSTS lists the shaders a sub-object draws with.
        Node header = child(object, "sub_object_header");
        Node shaders = child(header, "subobject_shaders");
        const int shader = stateNodes.size() > size_t(material) ? stateNodes[material].integer(1, -1) : -1;
        if (shaders && shader >= 0) {
            bool listed = false;
            QStringList values;
            for (int i = 0; i < shaders.scalarCount(); ++i) {
                values << shaders.scalar(i);
                listed = listed || (i > 0 && shaders.integer(i, -1) == shader);
            }
            if (!listed && !values.isEmpty()) {
                values[0] = QString::number(values.size());
                values << QString::number(shader);
                shaders.setScalars(values);
            }
        }
        // Later targets in the same sub-object: the extracted parts are
        // stale until re-extracted, but their order (drawn index) holds.
    }
    afterMaterialEdit();
    return true;
}

// The .sd's PBR lines (task shapes 13).
void SFileComplex::readPbrFactors() {
    d->pbrFactors.clear();
    if (!d->metadata)
        return;
    for (auto m : d->metadata->root.children("esd_tsrematerial")) {
        const QString key = MstsMaterials::key(m->scalar(0));
        QVector<float> values;
        for (int i = 1; i <= MstsMaterials::Factors::Count && !m->scalar(i).isEmpty(); ++i)
            values.push_back(float(m->number(i, std::numeric_limits<double>::quiet_NaN())));
        if (!key.isEmpty())
            d->pbrFactors.insert(key, MstsMaterials::Factors::read(values));
    }
}

bool SFileComplex::ensureMetadata(QString *error) {
    if (d->metadata)
        return true;
    // A .sd of its own: the name, no alternative textures, the bounds.
    const QString body = QStringLiteral("%1\r\n\tESD_Detail_Level ( 0 )\r\n\tESD_Alternative_Texture ( 0 )\r\n"
                                        "\tESD_Bounding_Box ( %2 %3 %4 %5 %6 %7 )")
            .arg(quoted(QFileInfo(d->path).fileName()))
            .arg(number(d->bound[1])).arg(number(d->bound[3])).arg(number(d->bound[5]))
            .arg(number(d->bound[0])).arg(number(d->bound[2])).arg(number(d->bound[4]));
    auto document = std::make_unique<SFileDetail::Document>();
    if (!metadataDocument(body, *document)) {
        if (error)
            //% "Cannot make the shape's .sd file."
            *error = qtTrId("shape.materials.error.sd");
        return false;
    }
    d->metadata = std::move(document);
    return true;
}

bool SFileComplex::setPbrFactors(const QString &key, const MstsMaterials::Factors &factors, QString *error) {
    if (key.isEmpty() || !ensureMetadata(error))
        return false;
    Node root = d->metadata->root;
    for (auto m : root.children("esd_tsrematerial"))
        if (MstsMaterials::key(m->scalar(0)) == key) {
            root.removeChild(m);
            break;
        }
    // Its textures decide what the defaults stand for: with an ORM texture
    // an unset metalness and roughness are 1.
    bool orm = false;
    for (const auto &mat : d->materials)
        if (mat.image >= 0 && MstsMaterials::key(d->images[mat.image].name) == key && mat.ormImage >= 0)
            orm = true;
    const QVector<float> values = factors.values(orm);
    if (!values.isEmpty()) {
        QString line = QStringLiteral("x ESD_TSREMaterial ( ") + quoted(key);
        for (float v : values)
            line += ' ' + number(v);
        line += QStringLiteral(" )");
        SFileDetail::Document snippet;
        const std::vector<Node> made = metadataDocument(line, snippet)
                ? list(snippet.root, "esd_tsrematerial") : std::vector<Node>();
        if (made.size() != 1) {
            if (error)
                *error = qtTrId("shape.materials.error.sd");
            return false;
        }
        root.insertCopy(made.front());
    }
    d->metadataEdited = true;
    readPbrFactors();
    resolvePbrFactors();
    return true;
}

bool SFileComplex::metadataEdited() const {
    return d->metadataEdited;
}

bool SFileComplex::saveMetadataFor(const QString &shapePath, QString *error) {
    if (!d->metadata) {
        if (error)
            *error = qtTrId("shape.materials.error.sd");
        return false;
    }
    // Its shape's file name.
    Node root = d->metadata->root;
    if (root.scalarCount() > 0)
        root.setScalar(0, QFileInfo(shapePath).fileName());
    QString e;
    const bool ok = d->metadata->save(ContentPath::withExtension(shapePath, "sd"), false, false, e);
    if (ok)
        d->metadataEdited = false;
    if (error)
        *error = e;
    return ok;
}

std::shared_ptr<const SFileComplex::Snapshot> SFileComplex::snapshot() const {
    auto s = std::make_shared<Snapshot>();
    if (d->document)
        s->document = std::make_unique<SFileDetail::Document>(*d->document);
    if (d->metadata)
        s->metadata = std::make_unique<SFileDetail::Document>(*d->metadata);
    s->metadataEdited = d->metadataEdited;
    return s;
}

qint64 SFileComplex::snapshotBytes(const Snapshot &snapshot) {
    return (snapshot.document ? snapshot.document->storageBytes() : 0)
            + (snapshot.metadata ? snapshot.metadata->storageBytes() : 0);
}

bool SFileComplex::restore(const Snapshot &snapshot, QString *error) {
    if (!editable(error) || !snapshot.document)
        return false;
    d->document = std::make_unique<SFileDetail::Document>(*snapshot.document);
    d->metadata = snapshot.metadata ? std::make_unique<SFileDetail::Document>(*snapshot.metadata) : nullptr;
    d->metadataEdited = snapshot.metadataEdited;
    readPbrFactors();
    afterMaterialEdit();
    return true;
}
