/*
  ==============================================================================

   This file is part of the YUP library.
   Copyright (c) 2024 - kunitoki@gmail.com

   YUP is an open source library subject to open-source licensing.

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   to use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   YUP IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

namespace yup
{

//==============================================================================
/** Represents a 2D geometric path.

    The Path class encapsulates a series of geometric operations and shapes that can be described
    using lines, curves, and basic geometric shapes. It provides methods to construct and manipulate
    these paths with operations such as moving to a point, drawing lines, curves, rectangles,
    rounded rectangles, ellipses, and arcs. It supports both simple constructs such as lines and
    complex cubic Bezier curves.

    This class uses an internal storage mechanism to keep track of each segment in the path,
    allowing for efficient modifications and rendering. The Path can be used for drawing operations,
    hit testing, and bounding box calculations.
*/
class YUP_API Path
{
public:
    //==============================================================================
    /** Represents the type of operation in a path segment. */
    enum class Verb
    {
        MoveTo,  /**< Move to a point without drawing. */
        LineTo,  /**< Draw a line to a point. */
        QuadTo,  /**< Draw a quadratic Bezier curve. */
        CubicTo, /**< Draw a cubic Bezier curve. */
        Close    /**< Close the current sub-path. */
    };

    //==============================================================================
    /** Represents a segment in a path with its verb and associated points. */
    struct Segment
    {
        Verb verb;                  /**< The type of path operation. */
        Point<float> point;         /**< The main point (end point for most operations). */
        Point<float> controlPoint1; /**< First control point for curves. */
        Point<float> controlPoint2; /**< Second control point for cubic curves. */

        /** Creates a MoveTo or LineTo segment. */
        Segment (Verb v, Point<float> p)
            : verb (v)
            , point (p)
            , controlPoint1 (0.0f, 0.0f)
            , controlPoint2 (0.0f, 0.0f)
        {
        }

        /** Creates a QuadTo segment. */
        Segment (Verb v, Point<float> p, Point<float> c1)
            : verb (v)
            , point (p)
            , controlPoint1 (c1)
            , controlPoint2 (0.0f, 0.0f)
        {
        }

        /** Creates a CubicTo segment. */
        Segment (Verb v, Point<float> p, Point<float> c1, Point<float> c2)
            : verb (v)
            , point (p)
            , controlPoint1 (c1)
            , controlPoint2 (c2)
        {
        }

        /** Creates a Close segment. */
        static Segment close()
        {
            return Segment (Verb::Close, Point<float> (0.0f, 0.0f));
        }
    };

    //==============================================================================
    /** Constructs an empty path. */
    Path();

    /** Initializes a path and moves to the specified coordinates.

        This constructor creates a path and immediately moves the current point to
        the coordinates (x, y), starting a new sub-path.

        @param x The x-coordinate to move to.
        @param y The y-coordinate to move to.
    */
    Path (float x, float y);

    /** Initializes a path and moves to the specified point.

        This constructor creates a path and immediately moves the current point to
        the given point, starting a new sub-path.

        @param p The point to move to.
    */
    Path (const Point<float>& p);

    //==============================================================================
    /** Copy and move constructors and assignment operators. */
    Path (const Path& other) noexcept = default;
    Path (Path&& other) noexcept = default;
    Path& operator= (const Path& other) noexcept = default;
    Path& operator= (Path&& other) noexcept = default;

    //==============================================================================
    /** Reserves memory for a specified number of segments.

        This method allocates memory in advance for a number of segments, potentially improving
        performance by reducing the number of memory allocations required as segments are added.

        @param numSegments The number of segments for which to reserve space.
    */
    void reserveSpace (int numSegments);

    /** Returns the number of segments in the path.

        This method counts the total number of segments that have been added to the path,
        including all types of moves and draws.

        @return The number of segments in the path.
    */
    int size() const;

    bool isEmpty() const { return size() == 0; }

    //==============================================================================
    /** Clears all the segments from the path.

        This method removes all segments from the path, effectively resetting it to an empty state.
    */
    void clear();

    //==============================================================================
    /** Moves the current point to specified coordinates, starting a new sub-path.

        This command does not produce any visible output but updates the current point to
        the specified coordinates. It starts a new sub-path that subsequent commands are considered
        part of.

        @param x The x-coordinate to move to.
        @param y The y-coordinate to move to.
    */
    Path& moveTo (float x, float y);

    /** Moves the current point to a specified point, starting a new sub-path.

        This command does not produce any visible output but updates the current point to
        the specified point. It starts a new sub-path that subsequent commands are considered
        part of.

        @param p The point to move to.
    */
    Path& moveTo (const Point<float>& p);

    //==============================================================================
    /** Draws a line from the current point to specified coordinates.

        This method draws a straight line from the current point to the coordinates (x, y),
        then updates the current point to these coordinates.

        @param x The x-coordinate of the endpoint.
        @param y The y-coordinate of the endpoint.
    */
    Path& lineTo (float x, float y);

    /** Draws a line from the current point to a specified point.

        This method draws a straight line from the current point to the specified point,
        then updates the current point to this new location.

        @param p The point to draw the line to.
    */
    Path& lineTo (const Point<float>& p);

    //==============================================================================
    /** Draws a quadratic Bezier curve to specified coordinates with one control point.

        This method draws a quadratic Bezier curve from the current point to the point (x, y),
        using (x1, y1) as the control point. The current point is then set to (x, y).

        @param x The x-coordinate of the endpoint.
        @param y The y-coordinate of the endpoint.
        @param x1 The x-coordinate of the control point.
        @param y1 The y-coordinate of the control point.
    */
    Path& quadTo (float x, float y, float x1, float y1);

    /** Draws a quadratic Bezier curve to specified coordinates with one control point.

        This method draws a quadratic Bezier curve from the current point to the point (x, y),
        using the given point p as the control point. The current point is then set to (x, y).

        @param p The control point.
        @param x1 The x-coordinate of the endpoint.
        @param y1 The y-coordinate of the endpoint.
    */
    Path& quadTo (const Point<float>& p, float x1, float y1);

    //==============================================================================
    /** Draws a cubic Bezier curve to specified coordinates with two control points.

        This method draws a cubic Bezier curve from the current point to the point (x, y), using
        (x1, y1) and (x2, y2) as the control points. The current point is then updated to (x, y).

        @param x The x-coordinate of the endpoint.
        @param y The y-coordinate of the endpoint.
        @param x1 The x-coordinate of the first control point.
        @param y1 The y-coordinate of the first control point.
        @param x2 The x-coordinate of the second control point.
        @param y2 The y-coordinate of the second control point.
    */
    Path& cubicTo (float x, float y, float x1, float y1, float x2, float y2);

    /** Draws a cubic Bezier curve to specified coordinates with two control points.

        This method draws a cubic Bezier curve from the current point to the endpoint (x1, y1),
        using the given point p and (x2, y2) as the control points. The current point is then updated
        to (x1, y1).

        @param p The first control point.
        @param x1 The x-coordinate of the second control point.
        @param y1 The y-coordinate of the second control point.
        @param x2 The x-coordinate of the endpoint.
        @param y2 The y-coordinate of the endpoint.
    */
    Path& cubicTo (const Point<float>& p, float x1, float y1, float x2, float y2);

    //==============================================================================
    /** Closes the current sub-path by drawing a line to the start point of the sub-path.

        This method completes the current sub-path by drawing a line back to the starting point
        of the sub-path, effectively closing any open figures and making them ready for fill
        operations. This does not affect the current point.
    */
    Path& close();

    //==============================================================================
    /** Adds a straight line segment to the path between two points.

        This method appends a line from point p1 to point p2 to the path. This operation updates
        the current point to p2.

        @param p1 The starting point of the line.
        @param p2 The ending point of the line.
    */
    Path& addLine (const Point<float>& p1, const Point<float>& p2);

    /** Adds a line segment described by a Line object to the path.

        This method appends the line specified by the Line object to the path, updating
        the current point to the end of the line.

        @param line The line to add to the path.
    */
    Path& addLine (const Line<float>& line);

    //==============================================================================
    /** Adds a rectangle to the path.

        This method appends a rectangle with the specified position and size to the path. The rectangle
        is added as a closed sub-path, starting and ending at the top-left corner.

        @param x The x-coordinate of the top-left corner of the rectangle.
        @param y The y-coordinate of the top-left corner of the rectangle.
        @param width The width of the rectangle.
        @param height The height of the rectangle.
    */
    Path& addRectangle (float x, float y, float width, float height);

    /** Adds a rectangle described by a Rectangle object to the path.

        This method appends the rectangle specified by the Rectangle object to the path,
        adding it as a closed sub-path, starting and ending at the top-left corner of the rectangle.

        @param rect The rectangle to add to the path.
    */
    Path& addRectangle (const Rectangle<float>& rect);

    //==============================================================================
    /** Adds a rounded rectangle to the path.

        This method appends a rounded rectangle with specified position, size, and corner radii
        to the path. Each corner can have a different radius, allowing for complex shapes.
        The rounded rectangle is added as a closed sub-path.

        @param x The x-coordinate of the top-left corner.
        @param y The y-coordinate of the top-left corner.
        @param width The width of the rectangle.
        @param height The height of the rectangle.
        @param radiusTopLeft The radius of the top-left corner.
        @param radiusTopRight The radius of the top-right corner.
        @param radiusBottomLeft The radius of the bottom-left corner.
        @param radiusBottomRight The radius of the bottom-right corner.
    */
    Path& addRoundedRectangle (float x, float y, float width, float height, float radiusTopLeft, float radiusTopRight, float radiusBottomLeft, float radiusBottomRight);

    /** Adds a rounded rectangle to the path.

        This method appends a rounded rectangle with specified position, size, and corner radius
        to the path. Each corner has the same radius, allowing for simple rounded corners.
        The rounded rectangle is added as a closed sub-path.

        @param x The x-coordinate of the top-left corner.
        @param y The y-coordinate of the top-left corner.
        @param width The width of the rectangle.
    */
    Path& addRoundedRectangle (float x, float y, float width, float height, float radius);

    /** Adds a rounded rectangle described by a Rectangle object with specific corner radii to the path.

        This method appends a rounded rectangle specified by the Rectangle object and corner radii
        to the path. Each corner of the rectangle can have a different radius, which provides flexibility
        in the appearance of the corners. The rounded rectangle is added as a closed sub-path.

        @param rect The rectangle to which rounded corners are to be added.
        @param radiusTopLeft The radius of the top-left corner.
        @param radiusTopRight The radius of the top-right corner.
        @param radiusBottomLeft The radius of the bottom-left corner.
        @param radiusBottomRight The radius of the bottom-right corner.
    */
    Path& addRoundedRectangle (const Rectangle<float>& rect, float radiusTopLeft, float radiusTopRight, float radiusBottomLeft, float radiusBottomRight);

    /** Adds a rounded rectangle to the path.

        This method appends a rounded rectangle with specified position, size, and corner radius
        to the path. Each corner has the same radius, allowing for simple rounded corners.
        The rounded rectangle is added as a closed sub-path.

        @param rect The rectangle to which rounded corners are to be added.
    */
    Path& addRoundedRectangle (const Rectangle<float>& rect, float radius);

    //==============================================================================
    /** Adds an ellipse to the path.

        This method appends an ellipse with the specified position and size to the path.
        The ellipse is defined within a bounding rectangle, starting and ending at the
        rightmost point of the ellipse.

        @param x The x-coordinate of the top-left corner of the bounding rectangle.
        @param y The y-coordinate of the top-left corner of the bounding rectangle.
        @param width The width of the bounding rectangle.
        @param height The height of the bounding rectangle.
    */
    Path& addEllipse (float x, float y, float width, float height);

    /** Adds an ellipse described by a Rectangle object to the path.

        This method appends an ellipse defined within the bounding rectangle specified
        by the Rectangle object to the path. The ellipse starts and ends at the rightmost
        point of the ellipse, forming a complete and closed sub-path.

        @param rect The rectangle that bounds the ellipse.
    */
    Path& addEllipse (const Rectangle<float>& rect);

    //==============================================================================
    /** Adds a centered ellipse to the path.

        This method appends an ellipse centered at (centerX, centerY) with specified radii.
        The ellipse starts and ends at the rightmost point of the ellipse, forming a complete
        and closed sub-path.

        @param centerX The x-coordinate of the center of the ellipse.
        @param centerY The y-coordinate of the center of the ellipse.
        @param radiusX The horizontal radius of the ellipse.
    */
    Path& addCenteredEllipse (float centerX, float centerY, float radiusX, float radiusY);

    /** Adds a centered ellipse to the path.

        This method appends an ellipse centered at the specified point with given radii.
        The ellipse starts and ends at the rightmost point of the ellipse, forming a complete
        and closed sub-path.

        @param center The center point of the ellipse.
        @param radiusX The horizontal radius of the ellipse.
    */
    Path& addCenteredEllipse (const Point<float>& center, float radiusX, float radiusY);

    /** Adds a centered ellipse to the path.

        This method appends an ellipse centered at the specified point with given diameter.
        The ellipse starts and ends at the rightmost point of the ellipse, forming a complete
        and closed sub-path.

        @param center The center point of the ellipse.
        @param diameter The diameter of the ellipse.
    */
    Path& addCenteredEllipse (const Point<float>& center, const Size<float>& diameter);

    //==============================================================================
    /** Adds an arc to the path.

        This method appends an arc defined within a bounding rectangle, between two radial angles.
        The arc can optionally start as a new sub-path or continue from the current point.

        @param x The x-coordinate of the top-left corner of the bounding rectangle.
        @param y The y-coordinate of the top-left corner of the bounding rectangle.
        @param width The width of the bounding rectangle.
        @param height The height of the bounding rectangle.
        @param fromRadians The starting angle of the arc, in radians.
        @param toRadians The ending angle of the arc, in radians.
        @param startAsNewSubPath Whether to start this as a new sub-path or continue from the current point.
    */
    Path& addArc (float x, float y, float width, float height, float fromRadians, float toRadians, bool startAsNewSubPath);

    /** Adds an arc described by a Rectangle object to the path.

        This method appends an arc that is defined within the bounding rectangle specified by
        the Rectangle object, between two radial angles. The arc can either start as a new sub-path
        or connect to the current point depending on the specified boolean.

        @param rect The rectangle that bounds the arc.
        @param fromRadians The starting angle of the arc, in radians.
        @param toRadians The ending angle of the arc, in radians.
        @param startAsNewSubPath Whether to start this as a new sub-path or continue from the current point.
    */
    Path& addArc (const Rectangle<float>& rect, float fromRadians, float toRadians, bool startAsNewSubPath);

    //==============================================================================
    /** Adds a centered arc to the path.

        This method appends an arc centered at (centerX, centerY) with specified radii and rotation,
        between two radial angles. The arc can start as a new sub-path or continue from the current point.

        @param centerX The x-coordinate of the center of the arc.
        @param centerY The y-coordinate of the center of the arc.
        @param radiusX The horizontal radius of the arc.
        @param radiusY The vertical radius of the arc.
        @param rotationOfEllipse The rotation angle of the ellipse, in radians.
        @param fromRadians The starting angle of the arc, in radians.
        @param toRadians The ending angle of the arc, in radians.
        @param startAsNewSubPath Whether to start this as a new sub-path or continue from the current point.
    */
    Path& addCenteredArc (float centerX, float centerY, float radiusX, float radiusY, float rotationOfEllipse, float fromRadians, float toRadians, bool startAsNewSubPath);

    /** Adds a centered arc described by a Point object to the path.

        This method appends an arc centered at the specified point with given radii and rotation,
        between two radial angles. The arc can either start as a new sub-path or connect to the current point
        depending on the specified boolean.

        @param center The center point of the arc.
        @param radiusX The horizontal radius of the arc.
        @param radiusY The vertical radius of the arc.
        @param rotationOfEllipse The rotation angle of the ellipse, in radians.
        @param fromRadians The starting angle of the arc, in radians.
        @param toRadians The ending angle of the arc, in radians.
        @param startAsNewSubPath Whether to start this as a new sub-path or continue from the current point.
    */
    Path& addCenteredArc (const Point<float>& center, float radiusX, float radiusY, float rotationOfEllipse, float fromRadians, float toRadians, bool startAsNewSubPath);

    /** Adds a centered arc described by a Point and Size object to the path.

        This method appends an arc centered at the specified point with given diameter and rotation,
        between two radial angles. The arc can either start as a new sub-path or connect to the current point
        depending on the specified boolean.

        @param center The center point of the arc.
        @param diameter The diameter of the arc.
        @param rotationOfEllipse The rotation angle of the ellipse, in radians.
    */
    Path& addCenteredArc (const Point<float>& center, const Size<float>& diameter, float rotationOfEllipse, float fromRadians, float toRadians, bool startAsNewSubPath);

    //==============================================================================
    /** Adds a triangle to the path.

        This method appends a triangle with the specified vertices to the path.

        @param x1 The x-coordinate of the first vertex.
        @param y1 The y-coordinate of the first vertex.
        @param x2 The x-coordinate of the second vertex.
        @param y2 The y-coordinate of the second vertex.
        @param x3 The x-coordinate of the third vertex.
        @param y3 The y-coordinate of the third vertex.
    */
    void addTriangle (float x1, float y1, float x2, float y2, float x3, float y3);

    /** Adds a triangle to the path.

        This method appends a triangle with the specified vertices to the path.

        @param p1 The first vertex of the triangle.
        @param p2 The second vertex of the triangle.
        @param p3 The third vertex of the triangle.
    */
    void addTriangle (const Point<float>& p1, const Point<float>& p2, const Point<float>& p3);

    //==============================================================================
    /** Adds a regular polygon to the path.

        This method appends a regular polygon with the specified number of sides, centered at the given point
        with the specified radius. The polygon starts at the given angle.

        @param centre The center point of the polygon.
        @param numberOfSides The number of sides for the polygon (minimum 3).
        @param radius The radius from the center to each vertex.
        @param startAngle The starting angle in radians (0.0f starts at the right).
    */
    Path& addPolygon (const Point<float>& centre, int numberOfSides, float radius, float startAngle = 0.0f);

    //==============================================================================
    /** Adds a star shape to the path.

        This method appends a star shape with the specified number of points, centered at the given point
        with inner and outer radii. The star starts at the given angle.

        @param centre The center point of the star.
        @param numberOfPoints The number of points for the star (minimum 3).
        @param innerRadius The radius from the center to the inner vertices.
        @param outerRadius The radius from the center to the outer vertices.
        @param startAngle The starting angle in radians (0.0f starts at the right).
    */
    Path& addStar (const Point<float>& centre, int numberOfPoints, float innerRadius, float outerRadius, float startAngle = 0.0f);

    //==============================================================================
    /** Adds a speech bubble shape to the path.

        This method creates a rounded rectangle with an arrow pointing to the specified tip position,
        suitable for speech bubbles or callout shapes.

        @param bodyArea The main rectangular area of the bubble.
        @param maximumArea The maximum area the bubble (including arrow) can occupy.
        @param arrowTipPosition The point where the arrow should point to.
        @param cornerSize The radius of the rounded corners.
        @param arrowBaseWidth The width of the arrow at its base.
    */
    Path& addBubble (const Rectangle<float>& bodyArea, const Rectangle<float>& maximumArea, const Point<float>& arrowTipPosition, float cornerSize, float arrowBaseWidth);

    //==============================================================================
    /** Converts the path to a stroke polygon with specified width.

        This method generates a closed polygon that represents the stroke of this path
        with the given stroke width. The resulting path can be filled to achieve the
        appearance of a stroked path.

        @param strokeWidth The width of the stroke.
        @return A new Path representing the stroke as a closed polygon.
    */
    Path createStrokePolygon (float strokeWidth) const;

    //==============================================================================
    /** Creates a new path with rounded corners applied to this path.

        This method generates a new path where sharp corners are replaced with
        rounded corners of the specified radius.

        @param cornerRadius The radius of the rounded corners.

        @return A new Path with rounded corners applied.
    */
    Path withRoundedCorners (float cornerRadius) const;

    //==============================================================================

    /** Starts a new sub-path at the specified point.

        This method starts a new sub-path at the given point, which becomes the current point.
        Sub-paths are used to create multiple disconnected shapes within a single path.

        @param x The x-coordinate of the starting point.
        @param y The y-coordinate of the starting point.
    */
    void startNewSubPath (float x, float y);

    /** Starts a new sub-path at the specified point.

        This method starts a new sub-path at the given point, which becomes the current point.
        Sub-paths are used to create multiple disconnected shapes within a single path.

        @param p The starting point of the sub-path.
    */
    void startNewSubPath (const Point<float>& p);

    /** Closes the current sub-path.

        This method closes the current sub-path by adding a line segment from the current point
        to the starting point of the sub-path. The current point is updated to the starting point.
    */
    void closeSubPath();

    //==============================================================================
    /** Checks if the current sub-path is closed.

        This method checks if the current sub-path is closed by comparing the current point
        to the starting point of the sub-path.

        @param tolerance The tolerance for the comparison.
    */
    bool isClosed (float tolerance = 0.001f) const;

    /** Checks if the current sub-path is explicitly closed.

        This method checks if the current sub-path is explicitly closed by checking if the
        last segment is a Close segment.
    */
    bool isExplicitlyClosed() const;

    //==============================================================================
    /** Appends another path to this one.

        This method appends all segments of another path to this path. It effectively concatenates
        the other path onto this one, continuing from the current point of this path.

        @param other The path to append to this path.
    */
    Path& appendPath (const Path& other);

    /** Appends another path to this one applying a transformation.

        This method appends all segments of another path to this path applying an affine transformation to its point
        before being added.

        @param other The path with transformation to append to this path.
    */
    Path& appendPath (const Path& other, const AffineTransform& transform);

    //==============================================================================
    /** Swaps the contents of this path with another path.

        This method efficiently swaps the internal data of this path with another path.

        @param other The path to swap with.
    */
    void swapWithPath (Path& other) noexcept;

    //==============================================================================
    /** */
    Path createCopy() const;

    //==============================================================================
    /** Transforms the path by applying an affine transformation.

        This method applies an affine transformation to the path, modifying its shape and position.
        The transformation is specified by an AffineTransform object.

        @param t The affine transformation to apply.

        @return A reference to this path after the transformation.
    */
    Path& transform (const AffineTransform& t);

    /** Returns a new path with the specified transformation applied.

        This method creates a new path with the same shape as this path, but with the specified
        transformation applied. The transformation is specified by an AffineTransform object.

        @param t The affine transformation to apply.

        @return A new Path with the transformation applied.
    */
    Path transformed (const AffineTransform& t) const;

    //==============================================================================
    /** Scales the path to fit within the specified bounds.

        This method transforms the path so that it fits within the given rectangular area.
        If preserveProportions is true, the aspect ratio is maintained.

        @param x The x-coordinate of the target area.
        @param y The y-coordinate of the target area.
        @param width The width of the target area.
        @param height The height of the target area.
        @param preserveProportions Whether to maintain the original aspect ratio.
    */
    void scaleToFit (float x, float y, float width, float height, bool preserveProportions) noexcept;

    //==============================================================================
    /** Returns the bounding box of this path.

        @return The bounding rectangle that contains all points in this path.
    */
    Rectangle<float> getBounds() const;

    /** Returns the bounding box of this path after applying a transformation.

        @param transform The transformation to apply before calculating the bounds.
        @return The bounding rectangle that contains all transformed points in this path.
    */
    Rectangle<float> getBoundsTransformed (const AffineTransform& transform) const;

    //==============================================================================
    /** Gets a point at a specific position along the path.

        This method returns a point located at the specified normalized distance along the path.
        The distance parameter should be between 0.0 (start of path) and 1.0 (end of path).

        @param distance The normalized distance along the path (0.0 to 1.0).
        @return The point at the specified distance along the path.
    */
    Point<float> getPointAlongPath (float distance) const;

    //==============================================================================
    /** Converts the path to an SVG path data string.

        This method converts the path to a string representation using SVG path data format.
        The resulting string can be used with fromString() to recreate the path.

        @return A string containing the SVG path data representation of this path.
    */
    String toString() const;

    /** Parses the path data from a string.

        This method parses the path data from a string in SVG path data format and updates the path accordingly.

        @param pathData The string containing the SVG path data.

        @return True if the path data was parsed successfully, false otherwise.
    */
    bool fromString (const String& pathData);

    //==============================================================================
    /** A forward iterator for iterating through path segments. */
    class Iterator
    {
    public:
        /** Creates an iterator for the given path. */
        Iterator (const rive::RawPath& rawPath, bool atEnd = false);

        /** Copy constructor. */
        Iterator (const Iterator& other) = default;

        /** Assignment operator. */
        Iterator& operator= (const Iterator& other) = default;

        /** Dereference operator to get the current segment. */
        Segment operator*() const;

        /** Pre-increment operator. */
        Iterator& operator++();

        /** Post-increment operator. */
        Iterator operator++ (int);

        /** Equality comparison. */
        bool operator== (const Iterator& other) const;

        /** Inequality comparison. */
        bool operator!= (const Iterator& other) const;

    private:
        void updateToValidPosition();
        Segment createCurrentSegment() const;

        const rive::RawPath* rawPath;
        size_t verbIndex;
        size_t pointIndex;
        bool isAtEnd;
    };

    /** Provides an iterator to the beginning of the path data.

        This method returns an iterator pointing to the first segment in the path's data.
        It allows for iteration over the path's segments from the beginning to the end.

        @return A PathIterator to the beginning of the path data.
    */
    Iterator begin();

    /** Provides a constant iterator to the beginning of the path data.

        This method returns a constant iterator pointing to the first segment in the path's
        data. It ensures that the path data cannot be modified during the iteration.

        @return A constant PathIterator to the beginning of the path data.
    */
    Iterator begin() const;

    /** Provides an iterator to the end of the path data.

        This method returns an iterator pointing just past the last segment in the path's data.
        It is used in conjunction with begin() for range-based iteration over the path's segments.

        @return A PathIterator to the end of the path data.
    */
    Iterator end();

    /** Provides a constant iterator to the end of the path data.

        This method returns a constant iterator pointing just past the last segment in the path's
        data. It ensures safe iteration over the path segments without modifying them.

        @return A constant PathIterator to the end of the path data.
    */
    Iterator end() const;

    //==============================================================================

    friend bool operator== (const Path& lhs, const Path& rhs) noexcept;
    friend bool operator!= (const Path& lhs, const Path& rhs) noexcept;

    //==============================================================================
    /** @internal Constructs a path from a raw render path. */
    explicit Path (rive::rcp<rive::RiveRenderPath> newPath);
    /** @internal Returns the raw render path to use in the renderer. */
    rive::RiveRenderPath* getRenderPath() const;
    /** @internal */
    void appendPath (rive::rcp<rive::RiveRenderPath> other);
    /** @internal */
    void appendPath (rive::rcp<rive::RiveRenderPath> other, const AffineTransform& transform);

private:
    rive::rcp<rive::RiveRenderPath> path;
};

} // namespace yup
